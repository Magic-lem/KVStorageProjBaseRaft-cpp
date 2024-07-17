//
// 客户端方法类的具体实现
// created by magic_pri on 2024-7-17
//

#iclude "clerk.h"

/*
Clerk::Get 函数
主要功能：获取数据库中指定键所对应的值
*/
std::sring Clerk::Get(std::string key) {
  m_requestId++;    // 增加发送的请求ID
  auto requestId = m_requestId;

  int server = m_recentLeaderId;    // raft节点ID，应该向Leader发送请求
  
  // 构造请求参数
  raftKVRpcProctoc::GetArgs args;
  args.set_key(key);
  args.set_clientid(m_clinetId);
  args.set_requestid(requestId);

  // 向目标raft节点发送请求，并处理返回消息
  while (true) {
    raftKVRpcProctoc::GetReply reply;
    // 发送请求
    bool ok = m_servers[server]->Get(&args, &reply);    // RPC调用
    if (!ok || reply.err() == ErrWrongLeader) {   // 如果通信失败或报领导错误，会一直更换其他raft节点重试
    // 因为requestId没有改变，因此可能会因为RPC的丢失或者其他情况导致重试，kvserver层会保证不重复执行（线性一致性），这里只需要重新发起就行，不用担心会重复执行
      server = (server + 1) % m_servers.size();
      continue;
    }
    if (reply.err() == ErrNoKey) {  // 没有相应的键，没找到
      return "";
    }
    if (reply.err() == OK) {  // 查询成功
      m_recentLeaderId = server;
      return reply.value();
    }
  }
  return "";
}

/*
Put 函数
主要功能：将指定的键值对插入到存储中，如果键已经存在，则覆盖其现有的值，借助于辅助函数PutAppend
*/
void Clerk::Put(std::string key, std::string value) { PutAppend(key, value, "Put"); }

/*
Append 函数
主要功能：将指定的值追加到键的现有值之后。如果键不存在，则等效于 Put 操作。借助于辅助函数PutAppend
*/
void Clerk::Append(std::string key, std::string value) { PutAppend(key, value, "Append"); }

/*
PutAppend 函数
主要功能：根据操作类型执行 Put 或 Append 操作。
*/
void Clerk::PutAppend(std::string key, std::string value, std::string op) {
  // 增加请求ID
  m_requestId++;
  auto requestId = m_requestId;
  auto server = m_recentLeaderId;

  while (true){
    // 构造请求参数，这里参数构造被放进了循环内部，原因？
    raftKVRpcProctoc::PutAppendArgs args;
    args.set_key(key);
    args.set_value(value);
    args.set_op(op);
    args.set_clientid(m_clientId);
    args.set_requestid(requestId);

    raftKVRpcProctoc::PutAppendReply reply;

    // 发送请求
    bool ok = m_servers[server]->PutAppend(&args, &reply);
    if (!ok || reply.err() == ErrWrongLeader) {
      DPrintf("【Clerk::PutAppend】原以为的leader: {%d}请求失败, 向新leader{%d}重试  ，操作：{%s}", server, server + 1, op.c_str());
      if (!ok) {
        DPrintf("重试原因 ,rpc通信失败");
      }
      if (reply.err() == ErrWrongLeader) {
        DPrintf("重试原因: 发送的目标不是leader");
      }
      server = (server + 1) % m_servers.size();  // try the next server
      continue;
    }
    if (reply.err() == OK) {
      m_recentLeaderId = server;
      return;
    }
  }
}

/*
Init 函数
主要功能：初始化客户端，建立与所有kvserver raft节点的RPC连接
*/
void Clerk::Init(std::string configFileName) {
  // 获取所有节点的IP和Port，进行连接
  MprpcConfig config;
  config.LoadConfigFile(configFileName.c_str());
  std::vector<std::pair<std::string, short>> ipPortVt;

  // 获取IP地址和端口号，存储到数组中
  for (int i = 0; i < INT_MAX - 1; ++i) {
    std::string node = "node" + std::to_string(i);
    std::string nodeIp = config.Load(node + "ip");
    std::string nodePortStr = config.Load(node + "port";

    if (nodeIp.empty()) {   // 没有节点
      break;
    } 

    ipPortVt.emplace_back(nodeIp, atoi(nodePortStr.c_str()));
  }

  // 进行连接
  for (cinst auto &item : ipPortVt) {
    std::string ip = item.first;
    short port = item.second;

    auto *rpc = new raftServerRpcUtil(ip, port);
    m_servers.push_back(std::shared_ptr<raftServerRpcUtil>(rpc));
  }
}

/*
构造函数
主要功能：初始化成员变量
*/
Clerk::Clerk() : m_clientId(Uuid()), m_requestId(0), m_recentLeaderId(0) {}


//
// KV数据库类方法的具体实现
// created by magic_pri on 2024-7-14
//

#include "kvServer.h"
#include "config.h"
#include "util.h"

/*
DprintfKVDB 函数
主要功能：打印键值数据库的内容
*/
void KvServer::DprintfKVDB() {
  if (!Debug) return;

  std::lock_guard<std::mutex> lg(m_mtx);

  DEFER {
    m_skipList.display_list();
  };
}

/*
ExecuteAppendOpOnKVDB 函数
主要功能：执行append功能，添加键值对
*/
void KvServer::ExecuteAppendOpOnKVDB(Op op) {
  m_mtx.lock();

  m_skipList.insert_set_element(op.Key, op.Value);   // 在跳表中添加键值对

  m_lastRequestId[op.ClientId] = op.RequestId;    // 更新下这个客户端的请求ID
  m_mtx.unlock();

  DprintfKVDB();    // 打印下数据库内容
}

/*
KvServer::ExecuteGetOpOnKVDB 函数
主要功能：执行get功能，获取指定键所对应的值
*/
void KvServer::ExecuteGetOpOnKVDB(Op op, std::string *value, bool *exist) {
  m_mtx.lock();

  // 初始化
  *value = "";
  *exist = false;

  if (m_skipList.search_element(op.Key, *value)) {    // 在跳表中查询
    *exist = true;
  }

  m_lastRequestId[op.ClientId] = op.RequestId;
  m_mtx.unlock();

  DprintfKVDB();
}

/*
KvServer::ExecutePutOpOnKVDB
主要功能：执行put功能，
*/
void KvServer::ExecutePutOpOnKVDB(Op op) {
  m_mtx.lock();
  m_skipList.insert_set_element(op.Key, op.Value);
  // m_kvDB[op.Key] = op.Value;
  m_lastRequestId[op.ClientId] = op.RequestId;
  m_mtx.unlock();

  //    DPrintf("[KVServerExePUT----]ClientId :%d ,RequestID :%d ,Key : %v, value : %v", op.ClientId, op.RequestId,
  //    op.Key, op.Value)
  DprintfKVDB();
}

/*
KvServer::Get 函数
主要功能：处理来自客户端的Get RPC请求（注意本函数不是RPC方法，而是在GetRPC方法中调用的处理函数）
*/
void KvServer::Get(const raftKVRpcProctoc::GetArgs *args, raftKVRpcProctoc::GetReply *reply) {
  // 1. 构造操作对象
  Op op;
  op.Operation = "Get";
  op.Key = args->key();
  op.Value = "";
  op.ClientId = args->clientid();
  op.RequestId = args->requestid();

  // 2. 向Raft集群提交操作（客户端发来了Get操作请求，需要通知raft节点将这个请求转换成日志条目，以实现各个raft节点之间的同步），要大部分raft节点同步了这个日志条目，Leader才会真正去执行这个操作请求。
  int raftIndex = -1;
  int _ = -1;
  bool isLeader = false;
  m_raftNode->Start(op, &raftIndex, &_, &isLeader);     // 让raft节点（Leader）开始处理客户端的命令请求，并构造日志条目
  if (!isLeader) {    // 说明本节点不是Leader，客户端不应该向非Leader节点发送请求，报错。
    reply->set_err(ErrWrongLeader);
    return;
  }

  // 3. 创建等待通道（线程安全队列），用于在操作提交后进行等待和同步Leader节点是否commit
  m_mtx.lock();
  if (waitApplyCh.find(raftIndex) == waitApplyCh.end()) {   // raftIndex是raft节点为新的日志条目生成的索引
    waitApplyCh.insert(std::make_pair(raftIndex, new LockQueue<Op>()));
  }
  auto chForRaftIndex = waitApplyCh[raftIndex];
  m_mtx.unlock(); //直接解锁，等待任务执行完成，不能一直拿锁等待

  // 4. 等待操作提交，并提供超时处理
  Op raftCommitOp;

  // 从通道中取出消息，并设置超时时间CONSENSUS_TIMEOUT，如果在时间内一直没能取出消息则触发超时
  if (!chForRaftIndex->timeOutPop(CONSENSUS_TIMEOUT, &raftCommitOp)) {      
    // 超时了，没有完成commit
    int _ = -1;
    bool isLeader = false;
    m_raftNode->GetState(&_, &isLeader);      
    // 检查请求是否重复以及当前节点是否仍为领导者
    if (ifRequestDuplicate(op.ClientId, op.RequestId) && isLeader) {    
      // 如果是重复的请求且节点是Leader，虽然超时了，raft集群不保证已经commitIndex该日志
      // 但是如果是已经提交过的get请求，是可以再执行的。不会违反线性一致性
      std::string value;
      bool exist = false;
      ExecuteGetOpOnKVDB(op, &value, &exist);   // 执行取出操作
      if (exist) {    // 成功取出
        reply->set_err(OK);   
        reply->set_value(value);
      } else {        // 失败，不存在该键
        reply->set_err(ErrNoKey);
        reply->set_value("");
      }
    } else {  // 否则，超时了即没完成
      reply->set_err(ErrWrongLeader);  //返回这个，其实就是让clerk换一个节点重试
    }
  } else {
    // 没超时，raft节点提交了这个命令，则可以正式执行了
    // 再次检验下leader提交的命令和当前命令是否一致：其实不用检验，leader只要正确的提交了，那么这些肯定是符合的
    if (raftCommitOp.ClientId == op.ClientId && raftCommitOp.RequestId == op.RequestId) {
      std::string value;
      bool exist = false;
      ExecuteGetOpOnKVDB(op, &value, &exist);
      if (exist) {
        reply->set_err(OK);
        reply->set_value(value);
      } else {
        reply->set_err(ErrNoKey);
        reply->set_value("");
      }
    } else {
      reply->set_err(ErrWrongLeader);
    }
  }
  // 销毁线程安全队列资源
  m_mtx.lock();  // todo 這個可以先弄一個defer，因爲刪除優先級並不高，先把rpc發回去更加重要
  auto tmp = waitApplyCh[raftIndex];
  waitApplyCh.erase(raftIndex);
  delete tmp;
  m_mtx.unlock();
}

/*
GetCommandFromRaft 函数
主要功能：kvserver 处理从 Raft 集群中收到的Put或Append命令，解析Raft日志条目，执行相应的操作，并处理与客户端请求相关的状态同步问题
*/
void KvServer::GetCommandFromRaft(ApplyMsg message) {
  // 1. 解析命令
  Op op;
  op.parseFromString(message.Command);    // 从string格式的命令获得op

  DPrintf("[KvServer::GetCommandFromRaft-kvserver{%d}] , Got Command --> Index:{%d} , ClientId {%s}, RequestId {%d}, Opreation {%s}, Key :{%s}, Value :{%s}", m_me, message.CommandIndex, &op.ClientId, op.RequestId, &op.Operation, &op.Key, &op.Value);

  // 2. 检查日志索引  
  if (message.CommandIndex <= m_lastSnapShotRaftLogIndex) {   
    // 命令的索引小于或等于最后的快照日志索引，说明该命令已经包含在快照中，不需要再次处理
    return;
  }

  // 3. 去重和执行命令
  if (!ifRequestDuplicate(op.ClientId, op.RequestId)) {   
    // put和append都不需要重复执行，如果重复了就不执行了
    if (op.Operation == "Put") {
      ExecutePutOpOnKVDB(op);
    }
    if (op.Operation == "Append") {
    ExecuteAppendOpOnKVDB(op);
    }
  }

  // 4. 检查是否需要快照
  if (m_maxRaftState != -1) {
    IfNeedToSendSnapShotCommand(message.CommandIndex, 9);   
  }

  // 5. 发送消息到等待通道，通知客户端此命令已经处理完毕
  SendMessageToWaitChan(op, message.CommandIndex);
}

/*
ifRequestDuplicate 函数
主要功能：检查客户端的请求是否是重复的
*/
bool KvServer::ifRequestDuplicate(std::string ClientId, int RequestId) {
  std::lock_guard<std::mutex> lg(m_mtx);
  if (m_lastRequestId.find(ClientId) == m_lastRequestId.end()) {    
    // 这个客户端没有发过请求
    return false;
  }

  return RequestId <= m_lastRequestId[ClientId];    // 与所记录的该客户端最后的请求序号比较
}

/*
KvServer::PutAppend函数
主要功能：处理客户端发起的PutAppend RPC请求，并将这些请求提交给 Raft 集群
注意：
    get和put/append执行的具体细节是不一样的
*/
void KvServer::PutAppend(const raftKVRpcProctoc::PutAppendArgs *args, raftKVRpcProctoc::PutAppendReply *reply) {
  // 1. 构造操作对象
  Op op;
  op.Operation = args->op();         // 设置操作类型（Put 或 Append）
  op.Key = args->key();              // 设置键
  op.Value = args->value();          // 设置值
  op.ClientId = args->clientid();    // 设置客户端ID
  op.RequestId = args->requestid();  // 设置请求ID

  // 2. 提交请求给raft节点
  int raftIndex = -1;   // 存储此命令所对应的日志索引
  int _ = -1;
  bool isLeader = false;
  m_raftNode->Start(op, &raftIndex, &_, &isLeader);

  // 3. 检查Leader状态：当前节点不是 Leader，返回 ErrWrongLeader 错误。
  if (!isLeader) {    
    reply->set_err(ErrWrongLeader);
    return;
  }

  // 4. 创建等待通道，等待 Raft 集群对该命令的处理结果
  m_mtx.lock();
  if (waitApplyCh.find(raftIndex) == waitApplyCh.end()) {
    waitApplyCh.insert(std::make_pair(raftIndex, new LockQueue<Op>()));
  }
  auto chForRaftIndex = waitApplyCh[raftIndex];
  m_mtx.unlock();

  // 5. 超时处理
  Op raftCommitOp;
  if (!chForRaftIndex->timeOutPop(CONSENSUS_TIMEOUT, &raftCommitOp)) {
    // 超时了，先判断请求是否是重复的
    if (ifRequestDuplicate(op.ClientId, op.RequestId)) {
      // 是重复的，不需要再操作，超时了也没事
      reply->set_err(OK);
    } else {
      reply->set_err(ErrWrongLeader);
    }
  } else {
    // 没超时，则说明leader节点已经提交命令了
    // 注意这里和Get方法的区别，put和append应用并不是在这里执行的，而是由后面的事件循环具体处理（实现异步）
    DPrintf(
    "[func -KvServer::PutAppend -kvserver{%d}]WaitChanGetRaftApplyMessage<--Server %d , get Command <-- Index:%d , "
    "ClientId %s, RequestId %d, Opreation %s, Key :%s, Value :%s",
    m_me, m_me, raftIndex, &op.ClientId, op.RequestId, &op.Operation, &op.Key, &op.Value);
    if (raftCommitOp.ClientId == op.ClientId && raftCommitOp.RequestId == op.RequestId) {
      reply->set_err(OK);
    } else {
      reply->set_err(ErrWrongLeader);
    }
  }

  m_mtx.lock();

  auto tmp = waitApplyCh[raftIndex];
  waitApplyCh.erase(raftIndex);
  delete tmp;
  m_mtx.unlock();
}

/*
ReadRaftApplyCommandLoop 函数
主要功能：实现了一个循环，监听并处理 Raft 协议应用的命令和快照，根据消息的类型进行处理
*/
void KvServer::ReadRaftApplyCommandLoop() {
  while (true) {
    //如果只操作applyChan不用拿锁，因为applyChan自己带锁
    auto message = applyChan->Pop();  // 弹出队头消息
    DPrintf(
        "---------------tmp-------------[func-KvServer::ReadRaftApplyCommandLoop()-kvserver{%d}] 收到了raft的消息",
        m_me);
    
    // 根据是命令还是快照具体执行
    if (message.CommandValid) {
      GetCommandFromRaft(message);
    }
    if (message.SnapshotValid) {
      GetSnapShotFromRaft(message);
    }
  }
}

/*
GetSnapShotFromRaft 函数
主要功能：从 Raft 协议接收快照并处理，将快照中的数据恢复到本地状态中
*/
void KvServer::GetSnapShotFromRaft(ApplyMsg message) {
  std::lock_guard<std::mutex> lg(m_mtx);

  if (m_raftNode->CondInstallSnapshot(message.SnapshotTerm, message.SnapshotIndex, message.Snapshot)) { // 将消息中的快照相关信息传递给 Raft 节点进行条件检查
    ReadSnapShotToInstall(message.Snapshot);  // 安装快照
    m_lastSnapShotRaftLogIndex = message.SnapshotIndex;
  }
}


/*
ReadSnapShotToInstall 函数
主要功能：将快照的状态信息恢复到当前 KvServer 实例中
*/
void KvServer::ReadSnapShotToInstall(std::string snapshot) {
  if (snapshot.empty()) {
    return;
  }

  parseFromString(snapshot);  // 反序列化，解析快照数据并加载到跳表中
}


/*
SendMessageToWaitChan 函数
主要功能：将 Raft 节点中提交的操作（Op 对象）发送到等待队列waitApplyCh中，以便让kvserver去执行具体的操作
*/
bool KvServer::SendMessageToWaitChan(const Op &op, int raftIndex) {
  std::lock_guard<std::mutex> lg(m_mtx);
  DPrintf(
      "[RaftApplyMessageSendToWaitChan--> raftserver{%d}] , Send Command --> Index:{%d} , ClientId {%d}, RequestId "
      "{%d}, Opreation {%v}, Key :{%v}, Value :{%v}",
      m_me, raftIndex, &op.ClientId, op.RequestId, &op.Operation, &op.Key, &op.Value);
  
  // 检查日志条目索引对应的等待队列是否存在
  if (waitApplyCh.find(raftIndex) == waitApplyCh.end()) {
    return false;
  }

  waitApplyCh[raftIndex]->Push(op);   // 向等待队列waitApplyCh添加元素
  DPrintf(
      "[RaftApplyMessageSendToWaitChan--> raftserver{%d}] , Send Command --> Index:{%d} , ClientId {%d}, RequestId "
      "{%d}, Opreation {%v}, Key :{%v}, Value :{%v}",
      m_me, raftIndex, &op.ClientId, op.RequestId, &op.Operation, &op.Key, &op.Value);
  return true;
}

/*
IfNeedToSendSnapShotCommand 函数
主要功能：检查是否需要制作快照，需要的话就向raft发送命令
*/
void KvServer::IfNeedToSendSnapShotCommand(int raftIndex, int proportion) {
  if (m_raftNode->GetRaftStateSize() > m_maxRaftState / 10.0) {   // raft节点的状态大小超过了阈值，需要制作快照
    auto snapshot = MakeSnapShot();   // 制作快照
    m_raftNode->Snapshot(raftIndex, snapshot);     // 让raft节点根据快照信息更新节点的日志条目
  }
}

/*
MakeSnapShot 函数
主要功能：将当前的跳表数据制作为快照（字符串）并返回
*/
std::string KvServer::MakeSnapShot() {
  std::lock_guard<std::mutex> lg(m_mtx);
  std::string snapshotData = getSnapshotData();
  return snapshotData;
}

/*-------------------------------------重写的RPC方法------------------------------------------*/
void KvServer::PutAppend(google::protobuf::RpcController *controller, const ::raftKVRpcProctoc::PutAppendArgs *request,
                         ::raftKVRpcProctoc::PutAppendReply *response, ::google::protobuf::Closure *done) {
  KvServer::PutAppend(request, response);   // 直接调用，类函数中对应的方法
  done->Run();  // 执行回调函数
}

void KvServer::Get(google::protobuf::RpcController *controller, const ::raftKVRpcProctoc::GetArgs *request,
                   ::raftKVRpcProctoc::GetReply *response, ::google::protobuf::Closure *done) {
  KvServer::Get(request, response);
  done->Run();
}


/*----------------------------------构造函数----------------------------------------------------*/
KvServer::KvServer(int me, int maxraftstate, std::string nodeInforFileName, short port):
                  m_skipList(6) {   // 初始化跳表，最高层级是6
  
  // 1. 初始化成员变量
  std::shared_ptr<Persister> persister = std::make_shared<Persister>(me);   // 初始化持久化对象
  // raft节点ID和节点存储最大值
  m_me = me;
  m_maxRaftState = maxraftstate;
  applyChan = std::make_shared<LockQueue<ApplyMsg> >();    // 初始化用于kvserver与raft节点通信的消息队列
  m_raftNode = std::make_shared<Raft>();    // 本kvserver所对应的raft节点


  // 2. 启动RPC服务
  std::thread t([this, port]() -> void {    // 通过一个新线程启动RPC服务，该线程专门负责事件的监听
    RpcProvider provider;   
    provider.NotifyService(this);   // 注册kvserver的RPC服务
    provider.NotifyService(this->m_raftNode.get());   // 注册所对应的raft节点的RPC服务
    provider.Run(m_me, port); // 开始提供服务，等待远程RPC调用请求
  });
  t.detach();  

  // 3. 等待所有raft节点启动
  std::cout << "raftServer node:" << m_me << " start to sleep to wait all ohter raftnode start!!!!" << std::endl;
  sleep(6); // 开启rpc远程调用能力，需要注意必须要保证所有节点都开启rpc接受功能之后才能开启rpc远程调用能力，这里使用睡眠来保证
  std::cout << "raftServer node:" << m_me << " wake up!!!! start to connect other raftnode" << std::endl;

  // 4. 加载所有raft节点的IP和端口信息
  MprpcConfig config;   
  config.LoadConfigFile(nodeInforFileName.c_str());
  std::vector<std::pair<std::string, short>> ipPortVt;
  for (int i = 0; i < INT_MAX - 1; ++i) {  // 循环读取配置文件中的节点IP和端口信息，直到读取不到新的节点为止，存储在ipPortVt向量中
    std::string node = "node" + std::to_string(i);

    std::string nodeIp = config.Load(node + "ip");
    std::string nodePortStr = config.Load(node + "port");
    if (nodeIp.empty()) {
      break;
    }
    ipPortVt.emplace_back(nodeIp, atoi(nodePortStr.c_str()));
  }

  // 5. 连接其他Raft节点
  std::vector<std::shared_ptr<RaftRpcUtil>> servers;    // 存储此raft节点与其他raft节点的通信对象
  for (int i = 0; i < ipPortVt.size(); ++i) {
    if (i == m_me) {  // 跳过自己
      servers.push_back(nullptr);
      continue;
    }
    std::string otherNodeIp = ipPortVt[i].first;
    short otherNodePort = ipPortVt[i].second;
    auto *rpc = new RaftRpcUtil(otherNodeIp, otherNodePort);    // 与目标raft节点建立连接
    servers.push_back(std::shared_ptr<RaftRpcUtil>(rpc));   // 存储到数组中

    std::cout << "node" << m_me << " 连接node" << i << "success!" << std::endl;
  }
  sleep(ipPortVt.size() - me);    // 等待所有节点之间的连接成功

  // 6. 初始化raft节点
  m_raftNode->init(servers, m_me, persister, applyChan);

  // 7. 检查是否存在快照进行恢复
  m_skipList;
  waitApplyCh;
  m_lastRequestId;
  m_lastSnapShotRaftLogIndex = 0;
  auto snapshot = persister->ReadSnapshot();
  if (!snapshot.empty()) {    // 快照不为空
    ReadSnapShotToInstall(snapshot);
  }

  // 8. 启动应用命令线程，持续运行处理Raft应用命令
  std::thread t2(&KvServer::ReadRaftApplyCommandLoop, this);
  t2.join();    // 通过 t2.join() 达到阻塞效果，确保 ReadRaftApplyCommandLoop 线程一直运行，不会被主线程提前结束
}
//
// KV数据库类方法的具体实现
// created by magic_pri on 2024-7-14
//

#include "kvServer.h"
#include "config.h"
#include "../common/include/util.h"

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
    if (ifRequestDuplicate(op.ClientId, op.RequestID) && isLeader) {    
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
  if (!ifRequestDuplicate(op.ClientId, op.RequestID)) {   
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

  return RequestID <= m_lastRequestId[ClientId];    // 与所记录的该客户端最后的请求序号比较
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
    if (ifRequestDuplicate(op.ClientId, op.RequestID)) {
      // 是重复的，不需要再操作，超时了也没事
      reply->set_err(OK);
    } else {
      reply->set_err(ErrWrongLeader);
    }
  } else {
    // 没超时，则说明leader节点已经提交命令了
    // 注意这里和Get方法的区别，put和append应用并不是在这里执行的，而是由后面的事件循环具体处理（实现异步）
    reply->set_err(OK);
  } else {
    reply->set_err(ErrWrongLeader);
  }
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


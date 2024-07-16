//
// raft的上层状态机——KV数据库的定义
// created by magic_pri on 2024-7-14
//

#ifndef SKIP_LIST_ON_RAFT_KVSERVER_H
#define SKIP_LIST_ON_RAFT_KVSERVER_H

#include <boost/any.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/foreach.hpp>
#include <boost/serialization/export.hpp>
#include <boost/serialization/serialization.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <boost/serialization/vector.hpp>
#include "kvServerRPC.pb.h"
#include "raft.h"
#include "skipList.h"
#include <iostream>
#include <mutex>
#include <unordered_map>

/*
键值存储服务器，它基于 Raft 共识算法实现。它通过 RPC 与客户端进行交互，同时通过 Raft 节点之间的通信保持一致性
*/
class KvServer : raftRpcProctoc::kvServerRPC {
public:
  KvServer() = delete;    // 禁用默认构造函数

  KvServer(int me, int m_maxRaftState, std::string nodeInforFileName, shor port);   // 含参构造函数

  void StartKVServer();   // 启动 KV 服务器

  void DprintfKVDB();     // 打印键值数据库的内容

  void ExecuteAppendOpOnKVDB(Op op);    // 执行 Append 操作

  void ExecuteGetOpOnKVDB(Op op, std::string *value, bool *exist);  // 执行 Get 操作

  void ExecutePutOpOnKVDB(Op op);  // 执行 Put 操作

  void Get(const raftKVRpcProctoc::GetArgs *args,raftKVRpcProctoc::GetReply *reply);  // 从raft节点中获得消息，不是执行Get操作

  void GetCommandFromRaft(ApplyMsg message);      //  从 Raft 节点获取命令

  bool ifRequestDuplicate(std::string ClientId, int RequestId);   // 检查请求是否重复

  void PutAppend(const raftKVRpcProctoc::PutAppendArgs *args, raftKVRpcProctoc::PutAppendReply *reply);   // 处理 Put 和 Append 请求

  void ReadRaftApplyCommandLoop();      // 循环读取 Raft 应用命令

  void ReadSnapShotToInstall(std::string snapshot);     // 读取并安装快照

  bool SendMessageToWaitChan(const Op &op, int raftIndex);      // 向等待通道发送消息

  // 检查是否需要制作快照，需要的话就向raft发送命令
  void IfNeedToSendSnapShotCommand(int raftIndex, int proportion);

  void GetSnapShotFromRaft(ApplyMsg message);   // 从 Raft 获取快照

  std::string MakeSnapShot();     // 制作快照

public:  // 重写的RPC方法
  void PutAppend(google::protobuf::RpcController *controller, const ::raftKVRpcProctoc::PutAppendArgs *request,
                 ::raftKVRpcProctoc::PutAppendReply *response, ::google::protobuf::Closure *done) override;

  void Get(google::protobuf::RpcController *controller, const ::raftKVRpcProctoc::GetArgs *request,
           ::raftKVRpcProctoc::GetReply *response, ::google::protobuf::Closure *done) override;

private:  // 序列化方法
  friend class boost::serialization::access;

  template <class Archive>
  void serialize(Archive &ar, const unsigned int version)  //  这里面写需要序列话和反序列化的字段
  {
    ar &m_serializedKVData;

    // ar & m_kvDB;
    ar &m_lastRequestId;
  }

  std::string getSnapshotData() {     // 序列化字段，获取快照数据
    m_serializedKVData = m_skipList.dump_file();
    std::stringstream ss;
    boost::archive::text_oarchive oa(ss);
    oa << *this;
    m_serializedKVData.clear();
    return ss.str();
  }

  void parseFromString(const std::string &str) {    // 反序列化，解析快照数据
    std::stringstream ss(str);
    boost::archive::text_iarchive ia(ss);
    ia >> *this;
    m_skipList.load_file(m_serializedKVData);
    m_serializedKVData.clear();
  }

private:
  std::mutex m_mtx;
  int m_me;   // 当前数据库标识符
  std::shared_ptr<Raft> m_raftNode;    // 当前kv数据库所对应的raft节点
  std::shared_ptr<LockQueue<<ApplyMsg>> applyChan;    //  Raft 节点与 KV 服务器之间通信的通道，是一个线程安全队列
  int m_maxRaftState;  // Raft 状态的最大值，用于判断是否需要进行快照。

  std::string m_serializedKVData;   // 序列化的键值数据
  SkipList<std::string, std::string> m_skipList;    // 使用跳表存储键值对
  std::unordered_map<std::string, std::string> m_kvDB;    // 键值数据库 

  std::unordered_map<int, LockQueue<Op> *>  waitApplyCh;    // 等待应用的操作通道，键为 Raft 的日志条目索引。

  std::unordered_map<std::sring, int> m_lastRequestId;    // 记录每个客户端的最后请求 ID，一个kV服务器可能连接多个client

  int m_lastSnapShotRaftLogIndex;    // 最后一个快照的日志条目索引
};

#endif
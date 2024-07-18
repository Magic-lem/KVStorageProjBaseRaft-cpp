//
// 客户端向kv数据库执行rpc通信的实现
// created by magic_pri on 2024-7-17
//

#ifndef RAFTSERVERRPC_H
#define RAFTSERVERRPC_H

#include <iostream>
#include "kvServerRPC.pb.h"
#include "mprpcchannel.h"
#include "mprpccontroller.h"
#include "rpcprovider.h"

/// @brief 维护客户端对kv server的rpc通信
// raftRpcUtil封装了各个raft节点之间的rpc通信
// 类似的raftServerRpcUtil封装的是所有客户端向kv server的rpc通信，如请求Get、Put等操作
class raftServerRpcUtil {
  public:
    // RPC方法
    bool Get(raftKVRpcProctoc::GetArgs* GetArgs, raftKVRpcProctoc::GetReply* reply);
    bool PutAppend(raftKVRpcProctoc::PutAppendArgs* args, raftKVRpcProctoc::PutAppendReply* reply);

    // 构造、析构函数
    raftServerRpcUtil(std::string ip, short port);
    ~raftServerRpcUtil();
  private:
    raftKVRpcProctoc::kvServerRpc_Stub* stub; // 客户端与服务器进行RPC通信的代理（存根）
};



#endif
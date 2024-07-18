//
// 管理Raft节点之间的RPC通信类 RaftRpcUtil
// created by magic_pri on 2024-6-19
//

#ifndef RAFTRPC_H
#define RAFTRPC_H

// 引入Raft RPC通信的消息和服务定义
#include "raftRPC.pb.h" // 这是个由protobuf生成的头文件
#include "mprpccontroller.h"
#include "mprpcchannel.h"

// @brief 维护当前节点对其他某一个节点的所有rpc发送通信的功能
// 对于一个raft节点来说，对于任意其他的节点都要维护一个通信实例和一个rpc连接（MprpcChannel）
// RaftRpcUtil封装了对其他节点的三种主要的Raft RPC方法的调用，使得这些调用更加简洁和易于使用。
// 实现方法是维护了当前raft节点的一个代理（存根）stub，使得能够通过这个stub访问该raft结点的函数
class RaftRpcUtil
{
public:
  RaftRpcUtil(std::string ip, short port);  // 构造函数，需要对方节点的IP和Port
  ~RaftRpcUtil();

  // 在proto中定义的三个方法，用于Raft协议中三个主要的RPC调用
  bool AppendEntries(raftRpcProctoc::AppendEntriesArgs *args, raftRpcProctoc::AppendEntriesReply *response);    // 日志复制和心跳信号
  bool InstallSnapshot(raftRpcProctoc::InstallSnapshotRequest *args, raftRpcProctoc::InstallSnapshotResponse *response);  // 安装快照
  bool RequestVote(raftRpcProctoc::RequestVoteArgs *args, raftRpcProctoc::RequestVoteReply *response);    // 请求投票

private:
  raftRpcProctoc::raftRpc_Stub *stub_;   // 代理（存根），用于调用远程RPC服务节点上的服务方法
};

#endif
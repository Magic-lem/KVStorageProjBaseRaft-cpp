//
// 管理Raft节点之间的RPC通信类 RaftRpcUtil
// created by magic_pri on 2024-6-19
//

#ifndef RAFTRPC_H
#define RAFTRPC_H

// 引入Raft RPC通信的消息和服务定义
#include "raftRPC.pb.h" // 这是个由protobuf生成的头文件

// @brief 维护当前节点对其他某一个节点的所有rpc发送通信的功能
// 对于一个raft节点来说，对于任意其他的节点都要维护一个rpc连接，即MprpcChannel
class RaftRpcUtil
{
public:
private:
};

#endif
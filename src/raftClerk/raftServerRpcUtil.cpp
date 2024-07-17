//
// 客户端向kv server的RPC调用的具体实现
// created by magic_pri on 2024-7-17
//
#include "raftServerRpcUtil.h"

/*
构造函数
主要功能：客户端初始化raftServerRpcUtil对象，并获得与kv server RPC通信的 stub
注意：
    kvserver不同于raft节点之间，kvserver的rpc是用于clerk向kvserver调用，不会被调用，因此只用写caller功能，不用写callee功能
*/
raftServerRpcUtil::raftServerRpcUtil(std::string ip, short port) {
  //*********************************************  */
  // 接收rpc设置
  //*********************************************  */

  // 发送rpc设置
  stub = new raftKVRpcProctoc::kvServerRpc_Stub(new MprpcChannel(ip, port, false));
}

raftServerRpcUtil::~raftServerRpcUtil() { delete stub; }

/*
Get 函数
主要功能：执行Get RPC调用，向服务器端发送请求消息
*/
bool raftServerRpcUtil::Get(raftKVRpcProctoc::GetArgs *GetArgs, raftKVRpcProctoc::GetReply *reply) {
  MprpcController controller;
  stub->Get(&controller, GetArgs, reply, nullptr);    // RPC调用，结果存在reply
  return !controller.Failed();
}

/*
PutAppend 函数
主要功能：执行PutAppend RPC调用，向服务器端发送请求消息
*/
bool raftServerRpcUtil::PutAppend(raftKVRpcProctoc::PutAppendArgs *args, raftKVRpcProctoc::PutAppendReply *reply) {
  MprpcController controller;
  stub->PutAppend(&controller, args, reply, nullptr);
  if (controller.Failed()) {
    std::cout << controller.ErrorText() << endl;
  }
  return !controller.Failed();
}
//
// Raft节点之间的RPC通信类 RaftRpcUtil的具体实现
// created by magic_pri on 2024-7-8
//

#include "./include/RaftRpcUtil.h"

/*
AppendEntries 方法
功能：Raft协议中的 AppendEntries 服务方法，用于日志复制和发送心跳信号。
参数：
    args：指向 AppendEntriesArgs 请求参数的指针。
    response：指向 AppendEntriesReply 响应参数的指针。
*/
bool RaftRpcUtil::AppendEntries(raftRpcProctoc::AppendEntriesArgs *args, raftRpcProctoc::AppendEntriesReply *response) {
  MprpcController controller;
  stub_->AppendEntries(&controller, args, response, nullptr);   // 调用注册的AppendEntries服务方法
  return !controller.Failed();
}

/*
InstallSnapshot 方法
功能：Raft协议中的 InstallSnapshot RPC调用，用于安装快照。
参数：
    args：指向 InstallSnapshotRequest 请求参数的指针。
    response：指向 InstallSnapshotResponse 响应参数的指针。
*/
bool RaftRpcUtil::InstallSnapshot(raftRpcProctoc::InstallSnapshotRequest *args,
                                  raftRpcProctoc::InstallSnapshotResponse *response) {
  MprpcController controller;
  stub_->InstallSnapshot(&controller, args, response, nullptr);
  return !controller.Failed();
}

/*
RequestVote 方法
功能：Raft协议中的 RequestVote RPC调用，用于投票请求。
参数：
    args：指向 RequestVoteArgs 请求参数的指针。
    response：指向 RequestVoteReply 响应参数的指针。
*/
bool RaftRpcUtil::RequestVote(raftRpcProctoc::RequestVoteArgs *args, raftRpcProctoc::RequestVoteReply *response) {
  MprpcController controller;
  stub_->RequestVote(&controller, args, response, nullptr);
  return !controller.Failed();
}


/*
构造函数
功能：初始化 RaftRpcUtil 对象，并设置与远端节点的RPC连接。
参数：
    ip：远端节点的IP地址。
    port：远端节点的端口号。
步骤：
    创建一个新的 MprpcChannel 对象，用于连接远端节点。
    使用 MprpcChannel 对象初始化一个新的 raftRpcProctoc::raftRpc_Stub 对象，并将其赋值给 stub_ 成员变量。
*/
RaftRpcUtil::RaftRpcUtil(std::string ip, short port) {
  //*********************************************  */
  //发送rpc设置
  stub_ = new raftRpcProctoc::raftRpc_Stub(new MprpcChannel(ip, port, true));
}

/*
析构函数
功能：释放 RaftRpcUtil 对象占用的资源。
*/
RaftRpcUtil::~RaftRpcUtil() {
  delete stub_;
}
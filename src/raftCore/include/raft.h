//
// Raft类
// created by magic_pri on 2024-6-19
//

#ifndef RAFT_H    // 预处理指令，防止头文件的重复包含
#define RAFT_H


#include <mutex>
#include <vector>
#include <memory>


/* 投票状态 */
// constexpr int Killed = 0;  // 编译器常量


class Raft{
public:

private:
    std::mutex m_mtx;    // mutex类对象，用于加互斥锁

    // 每个raft节点都需要与其他raft节点通信，所以要保存与其他节点通信的rpc入口
    std::vector<std::shared_ptr<RaftRpc>


};


#endif
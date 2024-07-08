//
// Raft类
// created by magic_pri on 2024-6-19
//

#ifndef RAFT_H    // 预处理指令，防止头文件的重复包含
#define RAFT_H


#include <mutex>
#include <vector>
#include <memory>
#include "RaftRpcUtil.h"


/* 投票状态 */
// constexpr int Killed = 0;  // 编译器常量

// Raft节点类
class Raft : public rafrRpcProctoc::raftRpc { // 继承自使用protobuf生成的raftRpc类
public:

private:
  std::mutex m_mtx;    // mutex类对象，用于加互斥锁

  // 每个raft节点都需要与其他raft节点通信，所以用一个数组保存与其他节点通信的rpc通信对象
  std::vector<std::shared_ptr<RaftRpcUtil>> m_peers; 

  int m_me;   // 当前节点的索引
  int m_currentTerm;    // 当前节点的任期号
  int m_votedFor;     // 当前节点在本任期内投票的候选人ID
  std::vector<rafrRpcProctoc::LogEntry> m_logs;  // 日志条目数组
  
  int m_commitIndex;    // 当前节点最大的已提交的日志条目索引
  int m_lastApplied;    // 已经应用到状态机的最大的日志条目索引
  std::vector<int> m_nextIndex;   // 发送给本服务器的下一个日志条目的索引
  std::vector<int> m_matchIndex;  // 本服务器已知的最大匹配日志条目的索引

  enum Status { Follower, Candidate, Leader };
  Status m_status;    // 当前节点的状态（Follower、Candidate、Leader）
  
  std::shared_ptr<LockQueue<ApplyMsg>> applyChan; 
  std::chrono::_V2::system_clock::time_point m_lastResetElectionTime;
  std::chrono::_V2::system_clock::time_point m_lastResetHearBeatTime;
  int m_lastSnapshotIncludeIndex;
  int m_lastSnapshotIncludeTerm;
  std::unique_ptr<monsoon::IOManager> m_ioManager = nullptr;


};


#endif
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
#include "ApplyMsg.h"

// 网络状态表示  todo：可以在rpc中删除该字段，实际生产中是用不到的.
// 方便网络分区的时候debug，网络异常的时候为disconnected，只要网络正常就为AppNormal，防止matchIndex[]数组异常减小
constexpr int Disconnected = 0;
constexpr int AppNormal = 1;

/* 投票状态 —— 编译期常量*/  
constexpr int Killed = 0;
constexpr int Voted = 1;   //本轮已经投过票了
constexpr int Expire = 2;  //投票（消息、竞选者）过期
constexpr int Normal = 3;


// Raft节点类
class Raft : public rafrRpcProctoc::raftRpc { // 继承自使用protobuf生成的raftRpc类
public:
  void AppendEntries1(const raftRpcProctoc::AppendEntriesArgs *args, raftRpcProctoc::AppendEntriesReply *reply);    // 实现 AppendEntries RPC 方法
  void applierTicker();     // 负责周期性地将已提交的日志应用到状态机
  bool CondInstallSnapshot(int lastIncludedTerm, int lastIncludedIndex, std::string snapshot);    // 条件安装快照
  void doElection();    // 发起选举
  void doHeartBeat();   // 发起心跳，只有leader才需要发起心跳

  void electionTimeOutTicker();         // 选举超时定时器
  std::vector<ApplyMsg> getApplyLogs();     // 获取应用的日志
  int getNewCommandIndex();   // 获取新命令的索引
  void getPrevLogInfo(int server, int *preIndex, int *preTerm);   // 获取前一个日志的信息
  void GetState(int *term, bool *isLeader);   // 获取当前节点的状态
  void InstallSnapshot(const raftRpcProctoc::InstallSnapshotRequest *args,    // 实现 InstallSnapshot RPC 方法
                       raftRpcProctoc::InstallSnapshotResponse *reply);
  void leaderHearBeatTicker();        // 领导者心跳定时器
  void leaderSendSnapShot(int server);    // 领导者发送快照
  void leaderUpdateCommitIndex();       // 领导者更新提交索引
  bool matchLog(int logIndex, int logTerm);   // 匹配日志
  void persist();         // 持久化当前状态
  void RequestVote(const raftRpcProctoc::RequestVoteArgs *args, raftRpcProctoc::RequestVoteReply *reply);   // 实现 RequestVote RPC 方法
  bool UpToDate(int index, int term);   // 检查日志是否是最新的
  int getLastLogIndex();    // 获取最后一个日志的索引
  int getLastLogTerm();     // 获取最后一个日志的任期
  void getLastLogIndexAndTerm(int *lastLogIndex, int *lastLogTerm);     // 获取最后一个日志的索引和任期
  int getLogTermFromLogIndex(int logIndex);       // 根据日志索引获取日志的任期
  int GetRaftStateSize();   // 获取 Raft 状态的大小
  int getSlicesIndexFromLogIndex(int logIndex);   // 根据日志索引获取切片索引

  bool sendRequestVote(int server, std::shared_ptr<raftRpcProctoc::RequestVoteArgs> args,     // 发送 RequestVote RPC 请求
                       std::shared_ptr<raftRpcProctoc::RequestVoteReply> reply, std::shared_ptr<int> votedNum);
  bool sendAppendEntries(int server, std::shared_ptr<raftRpcProctoc::AppendEntriesArgs> args,     // 发送 AppendEntries RPC 请求
                         std::shared_ptr<raftRpcProctoc::AppendEntriesReply> reply, std::shared_ptr<int> appendNums);

  void pushMsgToKvServer(ApplyMsg msg);     // 将消息推送到 KV 服务器
  void readPersist(std::string data);    // 读取持久化数据
  std::string persistData();    

  void Start(Op command, int *newLogIndex, int *newLogTerm, bool *isLeader);    // 开始一个新的命令

  // Snapshot the service says it has created a snapshot that has
  // all info up to and including index. this means the
  // service no longer needs the log through (and including)
  // that index. Raft should now trim its log as much as possible.
  // index代表是快照apply应用的index,而snapshot代表的是上层service传来的快照字节流，包括了Index之前的数据
  // 这个函数的目的是把安装到快照里的日志抛弃，并安装快照数据，同时更新快照下标，属于peers自身主动更新，与leader发送快照不冲突
  // 即服务层主动发起请求raft保存snapshot里面的数据，index是用来表示snapshot快照执行到了哪条命令
  void Snapshot(int index, std::string snapshot);   // 快照管理

public:
  // 重写基类（protobuf生成的raftRpc）方法,因为rpc远程调用真正调用的是这个方法
  // 序列化，反序列化等操作rpc框架都已经做完了，因此这里只需要获取值然后真正调用本地方法即可。
  void AppendEntries(google::protobuf::RpcController *controller, const ::raftRpcProctoc::AppendEntriesArgs *request,
                     ::raftRpcProctoc::AppendEntriesReply *response, ::google::protobuf::Closure *done) override;
  void InstallSnapshot(google::protobuf::RpcController *controller,
                       const ::raftRpcProctoc::InstallSnapshotRequest *request,
                       ::raftRpcProctoc::InstallSnapshotResponse *response, ::google::protobuf::Closure *done) override;
  void RequestVote(google::protobuf::RpcController *controller, const ::raftRpcProctoc::RequestVoteArgs *request,
                   ::raftRpcProctoc::RequestVoteReply *response, ::google::protobuf::Closure *done) override;


private:
  std::mutex m_mtx;    // mutex类对象，用于加互斥锁

  // 每个raft节点都需要与其他raft节点通信，所以用一个数组保存与其他节点通信的rpc通信对象
  std::vector<std::shared_ptr<RaftRpcUtil>> m_peers; 
  
  std::shared_ptr<Persister> m_persister;   // 用于持久化 Raft 状态的对象

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
  
  std::shared_ptr<LockQueue<ApplyMsg>> applyChan;     // 指向一个线程安全的日志队列，
  
  std::chrono::_V2::system_clock::time_point m_lastResetElectionTime;   // 最近一次重置选举计时器的时间
  std::chrono::_V2::system_clock::time_point m_lastResetHearBeatTime;   // 最近一次重置心跳计时器的时间
  int m_lastSnapshotIncludeIndex;     // 快照中包含的最后一个日志条目的索引
  int m_lastSnapshotIncludeTerm;      // 快照中包含的最后一个日志条目的任期号
  std::unique_ptr<monsoon::IOManager> m_ioManager = nullptr;    // 指向IO管理器，用于管理 I/O 操作
};


#endif
//
// raft节点类方法的具体实现
// created by magic_pri on 2024-7-9
//


#include "./include/raft.h"

/*
AppendEntries1：实现了 Raft 协议中的 AppendEntries RPC，用于将日志条目从领导者（Leader）发送到跟随者（Follower）
主要功能：处理领导者发送的追加日志条目请求，并根据接收到的请求更新跟随者的状态和日志
输入参数：   args
            reply
*/
void Raft::AppendEntries1(const raftRpcProctoc::AppendEntriesArgs* args, raftRpcProctoc::AppendEntriesReply* reply) {
  std::lock_guard<std::mutex> locker(m_mtx);    // RAII互斥锁
  reply->set_appstate(AppNormal);   // 表示网络正常
  // Your code here (2A, 2B).
  //	不同的人收到AppendEntries的反应是不同的，要注意无论什么时候收到rpc请求和响应都要检查term

  // 消息的任期号比当前节点的任期号小，说明是过期的消息
  if (args->term() < m_currentTerm) { 
    reply->set_success(false);
    reply->set_term(m_currentTerm);
    // 告诉领导者（Leader），当前节点拒绝了请求，因为领导者的任期号（term）比当前节点的任期号低，领导者需要及时更新其 nextIndex。设置 updatenextindex 为 -100 是一种特殊的标志，表示这种特定情况的拒绝。
    reply->set_updatenextindex(-100);   // 让领导人可以及时更新自己
    DPrintf("[func-AppendEntries-rf{%d}] 拒绝了 因为Leader{%d}的term{%v}< rf{%d}.term{%d}\n", m_me, args->leaderid(),
            args->term(), m_me, m_currentTerm);
    return;  // 注意从过期的领导人收到消息不要重设超时定时器
  }

  DEFER { persist(); }  // 本函数结束后执行持久化。执行persist的时候应该也是处于加锁状态的（locker还未释放）
  // 如果领导者的任期号大于当前任期号，更新当前任期号，并将状态设置为跟随者。
  if (args->term() > m_currentTerm) { 
    // 三变 ,防止遗漏，无论什么时候都是三变
    m_status = Follower;
    m_currentTerm = args->term();
    m_votedFor = -1;     // 这里设置成-1有意义，如果突然宕机然后上线理论上是可以投票的
  }

  myAssert(args->term() == m_currentTerm, format("assert {args.Term == rf.currentTerm} fail")); // 断言
  m_status = Follower;    // 收到了Leader的消息，设为Follower，这里是有必要的，因为如果candidate收到同一个term的leader的AE，需要变成follower
  m_lastResetElectionTime = now();    // 重置选举计时器

  // 不能无脑的从prevlogIndex开始添加日志，因为rpc可能会延迟，导致发过来的log是很久之前的
  // 比较leader之前已经同步日志的最大索引和当前节点的最新日志索引，有三种情况：
  if (args->prelogindex() > getLastLogIndex()) {
    // 情况1：领导者的 prevLogIndex 大于当前节点的 lastLogIndex，说明当前节点没更新之前的日志。当前节点无法处理这次 AppendEntries 请求，因为它没有 prevLogIndex 所指的日志条目（leader发送的日志太新了）
    reply->set_success(false);
    reply->set_term(m_currentTerm);
    reply->set_updatenextindex(getLastLogIndex() + 1);    // 向leader申请当前节点最后一个日志的下一个，没有用匹配加速
    return;
  } else if (args->prevlogindex() < m_lastSnapshotIncludeIndex) {
    // 情况2：领导者的 prevLogIndex 小于当前节点的 lastSnapshotIncludeIndex，说明领导者发送的日志条目已经被当前节点截断并快照化了，当前节点无法处理这些过时的日志条目。（leader发送的日志太老了）
    reply->set_success(false);
    reply->set_term(m_currentTerm);
    reply->set_updatenextindex(m_lastSnapshotIncludeIndex + 1);
  }
  // 情况3： prevLogIndex 在当前节点的日志范围内，需要进一步检查 prevLogTerm 是否匹配。
  // 情况3.1：prevLogIndex和prevLogTerm都匹配，还需要一个一个检查所有的当前新发送的日志匹配情况（有可能follower已经有这些新日志了）
  if (matchLog(args->prevlogindex(), args->prevlogterm())) {
    // 如果term也匹配。不能直接截断，必须一个一个检查，因为发送来的log可能是之前的，直接截断可能导致“取回”已经在follower日志中的条目
    // 不直接截断的处理逻辑是逐条检查和更新日志，而不是简单地从发现不匹配的位置开始删除所有后续日志条目。这是为了避免误删已经存在并且可能是正确的日志条目。
    for (int i = 0; i < args->entries_size(); i++) {
      auto log = args->entries(i);  // 遍历取出日志条目
      if (log.logindex() > getLastLogIndex()) {   //  超过follower中的最后一个日志，就直接添加日志
        m_logs.push_back(log);
      } else {
         // 没超过就说明follower已经有这些新日志了，比较是否匹配，不匹配再更新，而不是直接截断(直接截断有可能会造成丢失)
         if (m_logs[getSlicesIndexFromLogIndex(log.logindex())].logterm() == log.logterm() &&
             m_logs[getSlicesIndexFromLogIndex(log,logindex())].command() != log.command()) {
          // term和index相等，则两个日志应该也相等（raft基本性质），这里却不符合，出现异常
          myAssert(false, format("[func-AppendEntries-rf{%d}] 两节点logIndex{%d}和term{%d}相同，但是其command{%d:%d}   "
                                 " {%d:%d}却不同！！\n",
                                 m_me, log.logindex(), log.logterm(), m_me,
                                 m_logs[getSlicesIndexFromLogIndex(log.logindex())].command(), args->leaderid(),
                                 log.command()));
         }
         if (m_logs[getSlicesIndexFromLogIndex(log.logindex())].logterm() != log.logterm()) { // term不匹配
            m_logs[getSlicesIndexFromLogIndex(log.logindex())] = log;  // 相同的索引位置上发现日志条目的任期不同，意味着日志存在不一致（这个来自于旧的领导者，替换为新的）
         }
      }
    }
    // 验证更新后的最后一个日志索引是否正确，确保日志条目数量和索引的一致性
    myAssert(
        getLastLogIndex() >= args->prevlogindex() + args->entries_size(),   // 因为可能会收到过期的log！！！ 因此这里是大于等于
        format("[func-AppendEntries1-rf{%d}]rf.getLastLogIndex(){%d} != args.PrevLogIndex{%d}+len(args.Entries){%d}",
               m_me, getLastLogIndex(), args->prevlogindex(), args->entries_size()));
  
    // 更新提交索引，领导者的提交索引（args->leadercommit()落后于getLastLogIndex()的情况），或者是当前日志的最后一个索引（follower日志还没更新完全）
    if (args->leadercommit() > m_commitIndex) {
      m_commitIndex = std::min(args->leadercommit(), getLastLogIndex());
    }

    // l确保follower最后一个日志索引不小于follower提交索引
    myAssert(getLastLogIndex() >= m_commitIndex,
             format("[func-AppendEntries1-rf{%d}]  rf.getLastLogIndex{%d} < rf.commitIndex{%d}", m_me,
                    getLastLogIndex(), m_commitIndex));

    // 返回成功响应
    reply->set_success(true);
    reply->set_term(m_currentTerm);
    return;
  } else {   
    // 情况3.2：term不匹配
    // PrevLogIndex 长度合适，但是不匹配，因此往前寻找 矛盾的term的第一个元素
    // 为什么该term的日志都是矛盾的呢？也不一定都是矛盾的，只是这么优化减少rpc而已
    // 什么时候term会矛盾呢？很多情况，比如leader接收了日志之后马上就崩溃等等
    reply->set_updatenextindex(args->prevlogindex());   // 初始设置为prevlogindex
    // 从 prevlogindex 开始，向前查找与 prevlogterm 不同term的第一个日志条目，并更新 updatenextindex
    for (int index = args->prevlogindex(); index >= m_lastSnapshotIncludeIndex; --index) {  
    if (getLogTermFromLogIndex(index) != getLogTermFromLogIndex(args->prevlogindex())) {  // 使用了匹配加速，如果某一个日志不匹配，那么这一个日志所在的term的所有日志大概率都不匹配，直接找与他不同term的最后一个日志条目
      reply->set_updatenextindex(index + 1);
      break;
    }
    reply->set_success(false);
    reply->set_term(m_currentTerm);
    return;
  }
  }
}
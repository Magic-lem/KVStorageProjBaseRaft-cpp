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


/*
doHeartBeat 函数
主要功能：Raft协议中Leader节点的心跳发送函数
*/
void Raft::doHeartBeat() {
  std::lock_guard<std::mutex> g(m_mtx);  // 锁定互斥量以确保线程安全

  // 只有leader才需要发送心跳
  if (m_status == Leader) {
    DPrintf("[func-Raft::doHeartBeat()-Leader: {%d}] Leader的心跳定时器触发了且拿到mutex, 开始发送AE\n", m_me);
    auto appendNums = std::make_shared<int>(1);   // 统计正确返回的节点数量

    //对Follower（除了自己外的所有节点发送AE）
    // todo 这里肯定是要修改的，最好使用一个单独的goruntime来负责管理发送log，因为后面的log发送涉及优化之类的
    //最少要单独写一个函数来管理，而不是在这一坨
    for (int i = 0; i < m_peers.size(); i++) {
      if (i == m_me) {
        continue;
      }
      DPrintf("[func-Raft::doHeartBeat()-Leader: {%d}] Leader的心跳定时器触发了 index:{%d}\n", m_me, i);
      myAssert(m_nextIndex[i] >= 1, format("rf.nextIndex[%d] = {%d}", i, m_nextIndex[i]));  // 确保发送给Follower的日志索引在正常范围

      // 判断是发送AE（AppendEntries）还是快照
      if (m_nextIndex[i] <= m_lastSnapshotIncludeIndex) { // 需要发送的日志条目已经删除了，因为形成了快照，所要发送快照
        std::thread t(&Raft::leaderSendSnapShot, this, i);   // 创建新线程hi行发送快照函数
        t.detach();
        continue;
      }

      // 发送AE
      int preLogIndex = -1;
      int preLogTerm = -1;
      getPrevLogInfo(i, &preLogIndex, &preLogTerm); // 获取上次与第i个Follower同步的最后一个日志条目的索引和任期
 
      // 构造AE请求参数 AppendEntriesArgs
      std::shared_ptr<raftRpcProctoc::AppendEntriesArgs> appendEntriesArgs = std::make_shared<raftRpcProctoc::AppendEntriesArgs>(); 
      appendEntriesArgs->set_term(m_currentTerm);
      appendEntriesArgs->set_leaderid(m_me);
      appendEntriesArgs->set_prevlogindex(preLogIndex);
      appendEntriesArgs->set_prevlogterm(PrevLogTerm);
      appendEntriesArgs->clear_entries();
      appendEntriesArgs->set_leadercommit(m_commitIndex);

      // 添加日志条目
      if (preLogIndex != m_lastSnapshotIncludeIndex) {  
        // 前一个日志条目在快照之外，应该从该索引之后开始发送日志条目
        for (int j = getSlicesIndexFromLogIndex(prelogindex) + 1; j < m_logs.size(); ++j) {
          raftRpcProctoc::LogEntry* sendEntryPtr = appendEntriesArgs->add_entries();  // 返回一个指向新添加的日志条目的指针
          *sendEntryPtr = m_logs[j];  // 将该指针指向日志条目，则完成这个条目的添加    
        }
      } else {  // 即 preLogIndex == m_lastSnapshotIncludeIndex
        // 前一个日志条目正好是快照的分界点，那可以把Leader的所以日志条目都加入
        for (const auto& item: m_logs) {
          raftRpcProctoc::LogEntry* sendEntryPtr = appendEntriesArgs->add_entries();
          *sendEntryPtr = item; // =是可以点进去的，可以点进去看下protobuf如何重写这个赋值运算符的，实现直接将一个 protobuf 消息对象赋值给另一个消息对象
        }
      }

      // 检查：leader对每个节点发送的日志长短不一（每个follower的情况不同），但是都保证从prevIndex发送直到最后
      int lastLogIndex = getLastLogIndex();
      myAssert(appendEntriesArgs->prevlogindex() + appendEntriesArgs->entries_size() == lastLogIndex, format("appendEntriesArgs.PrevLogIndex{%d}+len(appendEntriesArgs.Entries){%d} != lastLogIndex{%d}", appendEntriesArgs->prevlogindex(), appendEntriesArgs->entries_size(), lastLogIndex));

      // 构造 AppendEntries 响应参数
      const std::shared_ptr<raftRpcProctoc::AppendEntriesReply> appendEntriesReply = std::make_shared<raftRpcProctoc::AppendEntriesReply>();
      appendEntriesReply->set_appstate(Disconnected);

      // 创建新线程，利用sendAppendEntries函数在新线程中接收并处理AppendEntries请求
      std::thread t(&Raft::sendAppendEntries, this, i, appendEntriesArgs, appendEntriesReply, appendNums);
      t.detach();
    }
    m_lastResetHearBeatTime = now();   // 更新上一次心跳时间
  }
}


/*
leaderHearBeatTicker 函数
主要功能：定时器功能，在 Raft 协议的 Leader 节点中用于控制定期向Follower节点发送心跳消息
*/
void Raft::leaderHearBeatTicker() {
  while (true) {
    //不是leader的话就没有必要进行后续操作，况且还要拿锁，很影响性能，目前是睡眠，后面再优化优化
    while (m_status != Leader) {
    usleep(1000 * HeartBeatTimeout);   // 如果不是 Leader，则休眠一段时间（HeartBeatTimeout 毫秒），然后重新检查
    }
    static std::atomic<int32_t> atomicCount = 0;    // 统计Leader节点的心跳定时器的执行次数

    std::chrono::duration<signed long int, std::ratio<1, 1000000000>> suitableSleepTime{};   // 记录合适的睡眠时间
    std::chrono::system_clock::time_point wakeTime{};    // 记录当前时间
    {  
    std::lock_guard<std::mutex> lock(m_mtx);   // 加锁
    wakeTime = now();
    // 睡眠时间 = 心跳超时时间 HeartBeatTimeout + 最后一次重置心跳时间 m_lastResetHearBeatTime - 当前时间
    suitableSleepTime = std::chrono::milliseconds(HeartBeatTimeout) + m_lastResetHearBeatTime - wakeTime;   
    }  // 出了这个域，已自动解锁

    if (std::chrono::duration<double, std::milli>(suitableSleepTime).count() > 1) {  // 适合睡眠时间大于 1 毫秒，则执行睡眠操作
      std::cout << atomicCount << "\033[1;35m leaderHearBeatTicker();函数设置睡眠时间为: "
                << std::chrono::duration_cast<std::chrono::milliseconds>(suitableSleepTime).count() << " 毫秒\033[0m"
                << std::endl;
      // 获取当前时间点
      auto start = std::chrono::steady_clock::now();    // 稳态时钟，在测量时间间隔时特别有用，因为它不会受到系统时间调整的影响，确保时间间隔测量的准确性和稳定性
      usleep(std::chrono::duration_cast<std::chrono::microseconds>(suitableSleepTime).count());

      // 获取睡眠结束后的时间点
      auto end = std::chrono::steady_clock::now();

      // 计算时间差并输出结果（单位为毫秒）
      std::chrono::duration<double, std::milli> duration = end - start;

      // 使用ANSI控制序列将输出颜色修改为紫色
      std::cout << atomicCount << "\033[1;35m leaderHearBeatTicker();函数实际睡眠时间为: " << duration.count() << " 毫秒\033[0m" << std::endl;
      ++atomicCount;
    }

    // 睡眠结束
    if (std::chrono::duration<double, std::milli>(m_lastResetHearBeatTime - wakeTime).count() > 0) {
      // 如果在睡眠的这段时间定时器被重置了，则再次睡眠
      continue;
    }

    // 执行心跳
    doHeartBeat();
  }
}

/*
sendAppendEntries 函数
主要功能：向指定的服务器发送附加日志条目（AppendEntries）请求
输入参数：
    server: raft节点ID，发送请求给该节点。
    args: 要发送的附加日志条目请求参数。
    reply: 用于存储服务器的回复。
    appendNums: 用于跟踪成功追加日志条目的服务器数量。
*/
bool Raft::sendAppendEntries(int server, std::shared_ptr<raftRpcProctoc::AppendEntriesArgs> args,
                             std::shared_ptr<raftRpcProctoc::AppendEntriesReply> reply,
                             std::shared_ptr<int> appendNums) {
  // 调用目标节点的重写的AppendEntries函数接收追加日志请求
  bool ok = m_peers->[server]->AppendEntries(args.get(), reply.get());  // RPC发挥远程调用的作用，注意智能指针要转换为裸指针

  // 检查网络通信
  //这个ok是网络是否正常通信的ok，而不是requestVote rpc是否投票的rpc
  // 如果网络不通的话肯定是没有返回的，不用一直重试
  // todo： paper中5.3节第一段末尾提到，如果append失败应该不断的retries ,直到这个log成功的被store
  if (!ok) {  // 通信失败
    DPrintf("[func-Raft::sendAppendEntries-raft{%d}] leader 向节点{%d}发送AE rpc失敗", m_me, server);
    return ok;
  }

  // 通信成功
  DPrintf("[func-Raft::sendAppendEntries-raft{%d}] leader 向节点{%d}发送AE rpc成功", m_me, server);
  
  if (reply->appstate() == Disconnected) {  // 通信成功，但是返回状态为Disconnected，远端 RPC 节点已经断连或不可用
    return ok;
  }
}

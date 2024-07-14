//
// raft节点类方法的具体实现
// created by magic_pri on 2024-7-9
//


#include "./include/raft.h"
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <memory>
#include "config.h"
#include "util.h"

/*** ---------------------------------------日志同步、心跳相关代码---------------------------------------------------- ***/

/*
AppendEntries ：重写了AppendEntries函数，是 Raft 协议中实现的 AppendEntries RPC 的入口，于处理来自领导者（Leader）的追加日志条目请求。
主要功能： RPC 入口，处理leader发送的添加日志请求，调用实际处理请求的 AppendEntries1 函数，然后调用 done->Run() 通知 RPC 框架请求已经处理完成
输入参数：
      google::protobuf::RpcController* controller: 用于控制 RPC 调用的对象（通常包含错误状态和其他控制信息）。
      const ::raftRpcProctoc::AppendEntriesArgs* request: 领导者发送的追加日志条目请求，包含了所有需要追加的日志条目及其他相关信息。
      ::raftRpcProctoc::AppendEntriesReply* response: 从节点返回给领导者的响应，包含了请求的处理结果。
      ::google::protobuf::Closure* done: 回调函数，用于在处理完成后通知 RPC 框架。

调用流程： 1. 领导者调用 stub.AppendEntries
          2. 上一步实际会进入到RpcChannel::CallMethod中，依托protobuf，根据调用的服务方法创建请求消息，发送到目标raft节点
          3. raft节点收到消息后进行处理（RpcProvider中的onMessage），根据消息中的方法描述符找到具体的函数执行，也就是本函数，并传入参数
*/
void Raft::AppendEntries(google::protobuf::RpcController* controller,
                         const ::raftRpcProctoc::AppendEntriesArgs* request,
                         ::raftRpcProctoc::AppendEntriesReply* response, ::google::protobuf::Closure* done) {
  AppendEntries1(request, response);
  done->Run();
}

/*
AppendEntries1：实现了 Raft 协议中的 AppendEntries RPC，用于将日志条目从领导者（Leader）发送到跟随者（Follower）
主要功能：Follower处理领导者发送的追加日志条目请求，并根据接收到的请求更新跟随者的状态和日志
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

  DEFER { persist(); };  // 本函数结束后执行持久化。执行persist的时候应该也是处于加锁状态的（locker还未释放）
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
  if (args->prevlogindex() > getLastLogIndex()) {
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
  // 情况3： prevLogIndex 在当前节点的日志范围内，需要进一步检查输入的logIndex所对应的log的任期是不是logterm。
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
             m_logs[getSlicesIndexFromLogIndex(log.logindex())].command() != log.command()) {
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

    // 对Follower（除了自己外的所有节点）发送AE
    // todo 这里肯定是要修改的，最好使用一个单独的goruntime来负责管理发送log，因为后面的log发送涉及优化之类的
    // 最少要单独写一个函数来管理，而不是在这一坨
    for (int i = 0; i < m_peers.size(); i++) {
      if (i == m_me) {
        continue;
      }
      DPrintf("[func-Raft::doHeartBeat()-Leader: {%d}] Leader的心跳定时器触发了 index:{%d}\n", m_me, i);
      myAssert(m_nextIndex[i] >= 1, format("rf.nextIndex[%d] = {%d}", i, m_nextIndex[i]));  // 确保发送给Follower的日志索引在正常范围

      // 判断是发送AE（AppendEntries）还是快照
      if (m_nextIndex[i] <= m_lastSnapshotIncludeIndex) { // 需要发送的日志条目已经删除了，因为形成了快照，所要发送快照
        std::thread t(&Raft::leaderSendSnapShot, this, i);   // 创建新线程执行发送快照函数
        t.detach();
        continue;
      }

      // 发送AE
      int preLogIndex = -1;
      int preLogTerm = -1;
      getPrevLogInfo(i, &preLogIndex, &preLogTerm); // 获取需要向第i个Follower发送的日志条目的上一个日志条目的索引和任期
 
      // 构造AE请求参数 AppendEntriesArgs
      std::shared_ptr<raftRpcProctoc::AppendEntriesArgs> appendEntriesArgs = std::make_shared<raftRpcProctoc::AppendEntriesArgs>(); 
      appendEntriesArgs->set_term(m_currentTerm);
      appendEntriesArgs->set_leaderid(m_me);
      appendEntriesArgs->set_prevlogindex(preLogIndex);
      appendEntriesArgs->set_prevlogterm(preLogTerm);
      appendEntriesArgs->clear_entries();
      appendEntriesArgs->set_leadercommit(m_commitIndex);

      // 添加日志条目
      if (preLogIndex != m_lastSnapshotIncludeIndex) {  
        // 前一个日志条目在快照之外，应该从该索引之后开始发送日志条目
        for (int j = getSlicesIndexFromLogIndex(preLogIndex) + 1; j < m_logs.size(); ++j) {
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
      usleep(std::chrono::duration_cast<std::chrono::microseconds>(suitableSleepTime).count());     //  钩子函数，让出协程的执行权

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
主要功能：向指定的raft节点发送附加日志条目（AppendEntries）请求，并处理该节点的回复reply来判断是否成功完成请求
输入参数：
    server: raft节点ID，发送请求给该节点。
    args: 要发送的附加日志条目请求参数。
    reply: 用于存储raft节点的回复。
    appendNums: 用于跟踪成功追加日志条目的服务器数量。
*/
bool Raft::sendAppendEntries(int server, std::shared_ptr<raftRpcProctoc::AppendEntriesArgs> args,
                             std::shared_ptr<raftRpcProctoc::AppendEntriesReply> reply,
                             std::shared_ptr<int> appendNums) {
  // 调用目标节点的重写的AppendEntries函数接收追加日志请求
  bool ok = m_peers[server]->AppendEntries(args.get(), reply.get());  // RPC发挥远程调用的作用，注意智能指针要转换为裸指针

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
  
  if (reply->appstate() == Disconnected) {  // 通信成功，但是返回状态为Disconnected，但由于各种原因（如服务器重启、网络分区等）,远端 RPC 节点已经断连或不可用
    return ok;
  }

  // 节点可用，处理节点返回的回复 
  std::lock_guard<std::mutex> lg1(m_mtx);   // 加锁，保护共享资源
  if (reply->term() > m_currentTerm) {  // 对端raft节点的term比当前节点的term更新
    m_status = Follower;  // 退为Follower
    m_currentTerm = reply->term();
    m_votedFor = -1;    // 重置投票记录
    return ok;
  } else if (reply->term() < m_currentTerm) { // 对端服务节点的term比当前的小，则这个reply是过期消息，不处理
    DPrintf("[func -sendAppendEntries  rf{%d}]  节点：{%d}的term{%d}<rf{%d}的term{%d}\n", m_me, server, reply->term(),
            m_me, m_currentTerm);
    return ok;
  }

  // 上述符合，则任期相等，则需要处理reply
  if (m_status != Leader) {     // 当前节点已经退回了Folloer，则不需要其处理reply
    return ok;
  }

  myAssert(reply->term() == m_currentTerm,
           format("reply.Term{%d} != rf.currentTerm{%d}   ", reply->term(), m_currentTerm));   // 断言检查
  
  if (!reply->success()) {
    // 回复中声明这次请求没有成功，则说明日志条目的index不匹配，正常来说就是index要往前-1来寻找最后一个匹配的日志条目
    // 第一个日志（idnex = 1）发送后肯定是匹配的，因此不用考虑变成负数
    if (reply->updatenextindex() != -100) {
      DPrintf("[func -sendAppendEntries  rf{%d}]  返回的日志term相等, 但是index不匹配, 回缩nextIndex[%d]: {%d}\n", m_me,
              server, reply->updatenextindex());
      m_nextIndex[server] = reply->updatenextindex();  // 直接退回到raft节点返回来的日志条目
    }
    //	怎么越写越感觉rf.nextIndex数组是冗余的呢，看下论文fig2，其实不是冗余的
  } else {
    // 请求成功，日志条目是匹配的，新的日志条目已经添加到了对端raft节点上
    *appendNums = *appendNums + 1;  // 成功+1
    DPrintf("---------------------------tmp------------------------- 节点{%d}返回true,当前*appendNums{%d}", server, *appendNums);
    // 更新这个对端Follower节点的日志条目信息
    m_matchIndex[server] = std::max(m_matchIndex[server], args->prevlogindex() + args->entries_size()); // Follower中当前匹配的最新日志条目索引号
    m_nextIndex[server] = m_matchIndex[server] + 1;   // Follower中下一个想要日志条目的索引号

    int lastLogIndex = getLastLogIndex();   // Leader中最新的日志索引

    myAssert(m_nextIndex[server] <= lastLogIndex + 1, // 肯定不能超过最新的，否则是不合理的
             format("error msg:rf.nextIndex[%d] > lastLogIndex+1, len(rf.logs) = %d   lastLogIndex{%d} = %d", server, m_logs.size(), server, lastLogIndex));

    // 检查是否可以提交日志条目（多数follower节点成功更新）
    if (*appendNums >= 1 + m_peers.size() / 2) {
      *appendNums = 0; // 重置

      // leader只有在当前term有日志提交的时候才更新commitIndex，因为raft无法保证之前term的Index是否提交
      // 只有当前term有日志提交，之前term的log才可以被提交，只有这样才能保证“领导人完备性{当选领导人的节点拥有之前被提交的所有log，当然也可能有一些没有被提交的}”
      if (args->entries_size() > 0) {
        DPrintf("args->entries(args->entries_size()-1).logterm(){%d}   m_currentTerm{%d}",
                args->entries(args->entries_size() - 1).logterm(), m_currentTerm);
      }

      if (args->entries_size() > 0 && args->entries(args->entries_size() - 1).logterm() == m_currentTerm) {    // 保证存在日志、且存在属于当前term的日志
        DPrintf(
            "---------------------------tmp------------------------- 當前term有log成功提交, 更新leader的m_commitIndex "
            "from{%d} to{%d}",
            m_commitIndex, args->prevlogindex() + args->entries_size());
        m_commitIndex = std::max(m_commitIndex, args->prevlogindex() + args->entries_size());    // 更新leader的提交索引m_commitIndex
      }

      // 检查，提交索引不应该超过最新的日志索引
      myAssert(m_commitIndex <= lastLogIndex,
               format("[func-sendAppendEntries,rf{%d}] lastLogIndex:%d  rf.commitIndex:%d\n", m_me, lastLogIndex,
                      m_commitIndex));

      // 这里只是提交了，具体应用还没有。（后续通过应用定时器）

    }
  }
  // 返回消息处理完毕
  return ok;  
}


/*
leaderSendSnapShot 函数
主要功能：当日志条目太多导致占用太多空间时，领导者可以创建一个快照并发送给跟随者。这个快照包含了某个 index 之前的所有状态，以减小日志的长度。
输入参数：
      int server    要发送给的follower
*/
void Raft::leaderSendSnapShot(int server) {
  // 加锁
  m_mtx.lock();

  // 构造快照请求 InstallSnapshotRequest
  raftRpcProctoc::InstallSnapshotRequest args;
  args.set_leaderid(m_me);
  args.set_term(m_currentTerm);
  args.set_lastsnapshotincludeindex(m_lastSnapshotIncludeIndex);
  args.set_lastsnapshotincludeterm(m_lastSnapshotIncludeTerm);
  args.set_data(m_persister->ReadSnapshot());

  // 构造响应 InstallSnapshotResponse
  raftRpcProctoc::InstallSnapshotResponse reply;

  // 解锁,以允许其他操作并行进行，防止阻塞其他操作。
  m_mtx.unlock();

  // 发送快照
  bool ok = m_peers[server]->InstallSnapshot(&args, &reply);  // RPC调用对端rpc节点，接受快照并安装，返回响应

  // 发送请求后再次加锁以处理回复和更新状态
  m_mtx.lock();
  DEFER { m_mtx.unlock(); };   // 本函数结束后自动解锁
  if (!ok) {  // 发送失败，不需要处理响应
    return;
  }

  if (m_status != Leader || m_currentTerm != args.term()) {
    return; //中间释放过锁，可能状态已经改变了，只有leader节点才需要处理reply
  }

  // 无论什么时候都要判断term
  if (reply.term() > m_currentTerm) {  // 当前节点的term落后了，则不能继续为领导，退回到Follower
    // 三变，更新当前任期并转换为跟随者
    m_status = Follower;
    m_currentTerm = reply.term();
    m_votedFor = -1;
    persist();    // 持久化当前状态
    m_lastResetElectionTime = now();  // 重置选举计时器的时间,防止立即开始新一轮的选举,给新的领导者足够的时间发送心跳和维持领导地位。
    return;
  }

  // 成功完成快照的发送和响应接收，更新所保存的追随者状态
  m_matchIndex[server] = args.lastsnapshotincludeindex();
  m_nextIndex[server] = m_matchIndex[server] + 1;
}

/*
InstallSnapshot 函数
主要功能：处理 Leader 发送的快照数据，更新当前节点的状态和日志，同时将快照应用到状态机并持久化
输入参数：
        const raftRpcProctoc::InstallSnapshotRequest* args,   快照安装请求消息
        raftRpcProctoc::InstallSnapshotResponse* reply        快照安装结果响应消息
*/
void Raft::InstallSnapshot(const raftRpcProctoc::InstallSnapshotRequest* args,
                           raftRpcProctoc::InstallSnapshotResponse* reply) {
  m_mtx.lock();
  DEFER { m_mtx.unlock(); };

  // 比较请求消息中的term和当前raft节点的term，如果请求消息中的term更小，则是过期的消息，拒绝快照
  if (args->term() < m_currentTerm) { 
    reply->set_term(m_currentTerm);
    return;
  }

  // 如果请求消息的term比当前节点的term更大，更新当前节点的 term 并转为 Follower
  if (args->term() > m_currentTerm) { 
    // 三变
    m_currentTerm = args->term();
    m_votedFor = -1;
    m_status = Follower;
    persist();  
  }

  m_status = Follower;
  m_lastResetElectionTime = now();  // 重置选举定时器（因为收到了来自leader的快照，不需要重新选举）

  // 比较快照的索引，如果收到的快照的索引小于等于当前节点的最后快照索引，说明是旧快照，忽略
  if (args->lastsnapshotincludeindex() <= m_lastSnapshotIncludeIndex) {
    return;
  }

  // 截断日志
  // 如果除了要生成快照的，还有更多的日志，则做一个截断，分解点之前的日志条目删除
  // 如果最大日志索引比快照要小，则直接全部删除，因为有了快照
  auto lastLogIndex = getLastLogIndex();

  if (lastLogIndex > args->lastsnapshotincludeindex()) {
    m_logs.erase(m_logs.begin(), m_logs.begin() + getSlicesIndexFromLogIndex(args->lastsnapshotincludeindex()) + 1);   // 删除快照中截止索引之前的
  } else {
    m_logs.clear();  // 本节点中所有的日志条目都在快照之前，直接全部删除
  }

  // 修改commitIndex和lastApplied，生成快照的日志条目一定是已经被应用到状态机里的
  m_commitIndex = std::max(m_commitIndex, args->lastsnapshotincludeindex());
  m_lastApplied = std::max(m_lastApplied, args->lastsnapshotincludeindex());

  m_lastSnapshotIncludeIndex = args->lastsnapshotincludeindex();
  m_lastSnapshotIncludeTerm = args->lastsnapshotincludeterm();

  // 构造响应消息
  reply->set_term(m_currentTerm);
  
  // 构造应用消息，安装快照，将快照应用到本节点上的状态机中
  ApplyMsg msg;
  msg.SnapshotValid = true;
  msg.Snapshot = args->data();
  msg.SnapshotTerm = args->lastsnapshotincludeterm();
  msg.SnapshotIndex = args->lastsnapshotincludeindex();

  // 新开一个线程，异步应用快照到 KV 服务器
  std::thread t(&Raft::pushMsgToKvServer, this, msg);
  t.detach();

  // 持久化当前状态和快照数据
  m_persister->Save(persistData(), args->data());
}

/*
pushMsgToKvServer 函数
主要功能：将消息应用到kv数据库中，在这里的实现是将其加入到一个队列，后续由其他方法管理
*/
void Raft::pushMsgToKvServer(ApplyMsg msg) { 
  applyChan->Push(msg);   // 将应用消息加入到一个线程安全的队列中
}  



/*** ---------------------------------------领导者选举相关代码---------------------------------------------------- ***/
/*
electionTimeOutTicker 函数
主要作用：实现了Raft协议中的一个定时器，用于检查是否需要启动领导者选举。如果当前节点不是领导者且超时，则发起选举。
*/
void Raft::electionTimeOutTicker() {
  while (true) {  // 整个函数在一个无限循环中运行，这样定时器会一直有效
    /**
      如果不睡眠，那么对于leader，这个函数会一直空转，浪费cpu。且加入协程之后，空转会导致其他协程无法运行，对于时间敏感的AE，会导致心跳无法正常发送导致异常
    */
    while (m_status == Leader) {  // Leader不会启动选举，如果本节点是Leader，则睡眠
      usleep(HeartBeatTimeout);   // 定时时间没有严谨设置，因为HeartBeatTimeout比选举超时一般小一个数量级，因此就设置为HeartBeatTimeout了
    }

    // 计算合适的睡眠时间
    std::chrono::duration<signed long int, std::ratio<1, 1000000000>> suitableSleepTime{};
    std::chrono::system_clock::time_point wakeTime{};
    {
      m_mtx.lock();
      wakeTime = now();
      suitableSleepTime = getRandomizedElectionTimeout() + m_lastResetElectionTime - wakeTime;  // 合适的睡眠时间
      m_mtx.unlock();
    }

    if (std::chrono::duration<double, std::milli>(suitableSleepTime).count() > 1) { // 所计算出来的合适的睡眠时间超过1ms
      // 获取当前时间点
      auto start = std::chrono::steady_clock::now();
      usleep(std::chrono::duration_cast<std::chrono::microseconds>(suitableSleepTime).count());   // 睡眠

      // 获取睡眠后的时间点
      auto end = std::chrono::steady_clock::now();

      // 计算睡眠时间（ms）
      std::chrono::duration<double, std::milli> duration = end - start;

      // 使用ANSI控制序列将输出颜色修改为紫色
      std::cout << "\033[1;35m electionTimeOutTicker();函数设置睡眠时间为: "
                << std::chrono::duration_cast<std::chrono::milliseconds>(suitableSleepTime).count() << " 毫秒\033[0m"
                << std::endl;
      std::cout << "\033[1;35m electionTimeOutTicker();函数实际睡眠时间为: " << duration.count() << " 毫秒\033[0m"
                << std::endl;

      if (std::chrono::duration<double, std::milli>(m_lastResetElectionTime - wakeTime).count() > 0) {
        // 在睡眠期间重置了定时器，则没有超时（Leader发送日志或心跳维持了），再次睡眠
        continue;
      }

      // 超时了，触发选举
      doElection();
    }
  }
}

/*
doElection 函数
主要功能：Raft协议中的选举过程，Follower节点成为候选人，并向其他节点发送投票请求
*/
void Raft::doElection() {
  std::lock_guard<std::mutex> g(m_mtx);

  // 不是leader才需要选举
  if (m_status != Leader) {
    DPrintf("[       ticker-func-rf(%d)              ]  选举定时器到期且不是leader, 开始选举 \n", m_me);
    // 当选举的时候定时器超时就必须重新选举，不然没有选票就会一直卡主
    // 重竞选超时，term也会增加的
    m_status = Candidate;

    // 开始新一轮选举
    m_currentTerm += 1;
    m_votedFor = m_me;    // 自己给自己投
    persist();   // 持久化当前状态

    // 初始票数为1，因为自己已经投了一票。
    std::shared_ptr<int> votedNum = std::make_shared<int>(1);  // 使用 make_shared 函数初始化投票数指针 !! 亮点

    // 重置定时器
    m_lastResetElectionTime = now();

    // 向所有其他节点发送请求投票的RPC
    for (int i = 0; i < m_peers.size(); i++) {
      if (i == m_me) {
        continue;
      }

      int lastLogIndex = -1, lastLogTerm = -1;
      getLastLogIndexAndTerm(&lastLogIndex, &lastLogTerm);  //获取最后一个log的term和index

      // 创建投票请求消息并初始化
      std::shared_ptr<raftRpcProctoc::RequestVoteArgs> requestVoteArgs = std::make_shared<raftRpcProctoc::RequestVoteArgs>();
      requestVoteArgs->set_term(m_currentTerm);
      requestVoteArgs->set_candidateid(m_me);  // 候选者ID
      requestVoteArgs->set_lastlogindex(lastLogIndex);   // 用来对比日志新旧
      requestVoteArgs->set_lastlogterm(lastLogTerm); 

      // 创建投票请求的响应消息
      auto requestVoteReply = std::make_shared<raftRpcProctoc::RequestVoteReply>();

      // 新开一个线程，异步执行发送请求消息函数sendRequestVote，传入构造好的请求消息
      std::thread t(&Raft::sendRequestVote, this, i, requestVoteArgs, requestVoteReply, votedNum);
      t.detach();

      // 异步执行，可以迅速释放当前函数的锁，提高并发性能
    }
  }
}

/*
sendRequestVote 函数
主要功能：向其他节点发送选票请求并处理它们的响应
输入参数：
      server：表示要发送选票请求的目标服务器索引。
      args：包含选票请求的参数，是 raftRpcProctoc::RequestVoteArgs 类型的共享指针。
      reply：用于接收选票响应的参数，是 raftRpcProctoc::RequestVoteReply 类型的共享指针。
      votedNum：用于记录获得的选票数目的共享整数指针。
*/
bool Raft::sendRequestVote(int server, std::shared_ptr<raftRpcProctoc::RequestVoteArgs> args, 
                            std::shared_ptr<raftRpcProctoc::RequestVoteReply> reply, std::shared_ptr<int> votedNum) {
  auto start = now();
  DPrintf("[func-sendRequestVote rf{%d}] 向server{%d} 发送 RequestVote 开始", m_me, m_currentTerm, getLastLogIndex());
  // 远程RPC调用注册的RequestVote服务方法
  bool ok = m_peers[server]->RequestVote(args.get(), reply.get());
  DPrintf("[func-sendRequestVote rf{%d}] 向server{%d} 发送 RequestVote 完毕, 耗时:{%d} ms", m_me, m_currentTerm,
          getLastLogIndex(), now() - start);
  
  if (!ok) {  // ok是说明通信是否成功的返回值，而不是RequestVote的。网络通信失败，直接返回
    return ok;
  }

  // 网络通信成功，可以处理返回的响应
  std::lock_guard<std::mutex> lg(m_mtx);  // 加锁

  // 处理收到的响应，首先检查term
  if (reply->term() > m_currentTerm) {  // 响应的term比当前节点的term大，当前节点落后
    // 三变：身份、term、投票状态
    m_status = Follower;
    m_currentTerm = reply->term();
    m_votedFor = -1;
    persist();  // 持久化
    return true;
  } else if (reply->term() < m_currentTerm) {   // 过期的消息
    return true;
  }
  // 检查term应该一致
  myAssert(reply->term() == m_currentTerm, format("assert {reply.Term==rf.currentTerm} fail"));

  // 如果投票被拒绝，直接返回
  if (reply->votegranted()) {
    return true;
  }

  // 获得投票成功，增加选票数量
  *votedNum = *votedNum + 1;

  // 如果已经获得多数节点的选票，则成为Leader
  if (*votedNum >= m_peers.size() / 2 + 1) {
    votedNum = 0;
    if (m_status == Leader) {   // 已经是Leader了，不进行下一步
      myAssert(false,
               format("[func-sendRequestVote-rf{%d}]  term:{%d} 同一个term当两次领导, error", m_me, m_currentTerm));
    }

    m_status = Leader;
    DPrintf("[func-sendRequestVote rf{%d}] elect success  ,current term:{%d} ,lastLogIndex:{%d}\n", m_me, m_currentTerm,
            getLastLogIndex());
    
    // 初始化 Leader 相关状态，启动心跳线程
    int lastLogIndex = getLastLogIndex();
    for (int i = 0; i < m_nextIndex.size(); i++) {
      m_nextIndex[i] = lastLogIndex + 1;      // 下一个
      m_matchIndex[i] = 0;      //每换一个领导都是从0开始，论文中图2
    }
    std::thread t(&Raft::doHeartBeat, this);   // 向其他节点发送心跳，表示自己是Leader
    t.detach();

    persist();  // 持久化当前状态
  }

  return true;
}

/*
RequestVote 重写了RequestVote函数，是 Raft 协议中实现的 RequestVote RPC 的入口。
主要功能：节点处理来自其他节点的投票请求
输入参数：
          args：是包含请求选票的参数，类型为 raftRpcProctoc::RequestVoteArgs*。
          reply：是用于存储响应结果的参数，类型为 raftRpcProctoc::RequestVoteReply*。
*/
void Raft::RequestVote(const raftRpcProctoc::RequestVoteArgs* args, raftRpcProctoc::RequestVoteReply* reply) {
  std::lock_guard<std::mutex> lg(m_mtx);

  DEFER { persist(); };   // 函数结束后持久化

  // 对比请求消息和当前节点的term，有三种情况。
  // 情况1：竞选者发送的请求消息中的term比当前节点中的小。出现此现象的reason: 出现网络分区，该竞选者已经OutOfDate(过时）
  if (args->term() < m_currentTerm) {   // 请求消息是旧的，直接返回
    reply->set_term(m_currentTerm);
    reply->set_votestate(Expire);   // 投票结果是超时
    reply->set_votegranted(false);  // 没有给投票
    return;
  }

  // 如果任何时候rpc请求或者响应的term大于自己的term，更新term，并变成follower
  if (args->term() > m_currentTerm) {
    m_status = Follower;
    m_currentTerm = args->term();
    m_votedFor = -1;
  }

  myAssert(args->term() == m_currentTerm,
           format("[func--rf{%d}] 前面校验过args.Term==rf.currentTerm, 这里却不等", m_me));

 
  // 然后，检查log的term和index是不是匹配的
  int lastLogTerm = getLastLogTerm();
  if (!UpToDate(args->lastlogindex(), args->lastlogterm())) {   // 候选者的日志比当前节点的日志旧，拒绝投票
    reply->set_term(m_currentTerm);
    reply->set_votestate(Voted);
    reply->set_votegranted(false);
    return;
  }

  // 处理已投票状态
  if (m_votedFor != -1 && m_votedFor != args->candidateid()) {
    // 如果当前节点已经投票给其他候选者，拒绝再次投票
    reply->set_term(m_currentTerm);
    reply->set_votestate(Voted);
    reply->set_votegranted(false);
    return;
  } else {
    // 否则，当前节点投票给候选者，更新投票时间和状态
    m_votedFor = args->candidateid();
    m_lastResetElectionTime = now();  // 重置选举定时器
    reply->set_term(m_currentTerm);
    reply->set_votestate(Normal);
    reply->set_votegranted(true);
    return;
  }
}

/*
UpToDate 函数
主要功能：比较输入的日志term和index，与当前节点最新日志的term和index，如果输入的更新，则返回true
*/
bool Raft::UpToDate(int index, int term) {
  // lastEntry := rf.log[len(rf.log)-1]

  int lastIndex = -1;
  int lastTerm = -1;
  getLastLogIndexAndTerm(&lastIndex, &lastTerm);
  return term > lastTerm || (term == lastTerm && index >= lastIndex);
}



/*** ---------------------------------------------提交日志，向状态机应用---------------------------------------------------- ***/

/*
applierTicker：定时器功能，raft向状态机定时写入
主要作用：通过定期检查是否有新的日志需要应用，并将这些日志条目应用到状态机上（即 kvserver）
*/
void Raft::applierTicker() {
  while (true) {    // 在一个无限循环中运行，使其能够持续地检查和应用日志条目。
    m_mtx.lock();
    if (m_status == Leader) {
      DPrintf("[Raft::applierTicker() - raft{%d}]  m_lastApplied{%d}   m_commitIndex{%d}", m_me, m_lastApplied,
              m_commitIndex);
    }

    // 获取要应用的日志条目
    auto applyMsgs = getApplyLogs();
    m_mtx.unlock();   // 可以解锁，因为下面applyChan是个线程安全的，早点解锁要保证并发性能

    // 检查并处理应用日志条目
    if (!applyMsgs.empty()) {
      DPrintf("[func- Raft::applierTicker()-raft{%d}] 向kvserver報告的applyMsgs長度爲：{%d}", m_me, applyMsgs.size());
    }

    // 循环将所有应用消息应用到状态机上
    for (auto& message: applyMsgs) {
      applyChan->Push(message);   // 这是个线程安全的队列
    }

    sleepNMilliseconds(ApplyInterval);   // 睡眠一段时间，再检测
  }
}


/*
getApplyLogs： 获得要应用的日志，并将其打包为固定类型
主要功能：将需要应用的日志条目打包成 ApplyMsg 对象并返回一个包含这些对象的数组
*/
std::vector<ApplyMsg> Raft::getApplyLogs() {
  std::vector<ApplyMsg> applyMsgs;
  // 确保提交的索引不超过日志的最后一个索引
  myAssert(m_commitIndex <= getLastLogIndex(), format("[func-getApplyLogs-rf{%d}] commitIndex{%d} >getLastLogIndex{%d}",
                                                      m_me, m_commitIndex, getLastLogIndex()));
  // 将要提交应用的日志逐个打包
  while (m_lastApplied < m_commitIndex) {
    m_lastApplied++;
    // 确保日志条目的索引与 m_lastApplied 一致。
    myAssert(m_logs[getSlicesIndexFromLogIndex(m_lastApplied)].logindex() == m_lastApplied,
             format("rf.logs[rf.getSlicesIndexFromLogIndex(rf.lastApplied)].LogIndex{%d} != rf.lastApplied{%d} ",
                    m_logs[getSlicesIndexFromLogIndex(m_lastApplied)].logindex(), m_lastApplied));

    // 构造该日志的应用消息对象并初始化
    ApplyMsg applyMsg;   
    applyMsg.CommandValid = true;     // 表示是有效的日志命令，而不是快照
    applyMsg.SnapshotValid = false;   // 表示不是快照
    applyMsg.Command = m_logs[getSlicesIndexFromLogIndex(m_lastApplied)].command();   // 该消息的命令就是日志条目的命令
    applyMsg.CommandIndex = m_lastApplied;    // 日志条目的索引

    // 加入到数组中
    applyMsgs.emplace_back(applyMsg);
  }

  return applyMsgs;
}

/*** -------------------------------------------- 持久化 ---------------------------------------------------- ***/

/*
persist 函数
主要功能：将当前raft节点的状态持久化，写入到文件中
*/
void Raft::persist() {
  // Your code here (2C).
  auto data = persistData();
  m_persister->SaveRaftState(data);
  // fmt.Printf("RaftNode[%d] persist starts, currentTerm[%d] voteFor[%d] log[%v]\n", rf.me, rf.currentTerm,
  // rf.votedFor, rf.logs) fmt.Printf("%v\n", string(data))
}


/*
GetRaftStateSize 函数
主要功能：获得当前Raft节点状态持久化数据的大小
*/
int Raft::GetRaftStateSize() { return m_persister->RaftStateSize(); }


/*
persistData 函数
主要作用：利用 Boost 序列化库将当前 Raft 节点的状态序列化为字符串
*/
std::string Raft::persistData() {
  // 创建一个用于持久化的结构体对象
  BoostPersistRaftNode boostPersistRaftNode;

  // 将当前 Raft 节点的状态保存到该对象中
  boostPersistRaftNode.m_currentTerm = m_currentTerm;  // 当前任期
  boostPersistRaftNode.m_votedFor = m_votedFor;        // 投票给的候选人
  boostPersistRaftNode.m_lastSnapshotIncludeIndex = m_lastSnapshotIncludeIndex;  // 上次快照包含的日志索引
  boostPersistRaftNode.m_lastSnapshotIncludeTerm = m_lastSnapshotIncludeTerm;    // 上次快照包含的任期

  // 将日志条目序列化并保存到对象中
  for (auto& item : m_logs) {
    boostPersistRaftNode.m_logs.push_back(item.SerializeAsString());
  }

  // 使用 stringstream 和 Boost 序列化库将对象序列化为字符串
  std::stringstream ss;
  boost::archive::text_oarchive oa(ss);
  oa << boostPersistRaftNode;

  // 返回序列化后的字符串
  return ss.str();
}


/*
readPersist 函数
主要功能：基于boost序列化库，从持久化字符串数据中恢复 Raft 节点的状态
*/
void Raft::readPersist(std::string data) {
  if (data.empty()) {
    return;
  }
  // 初始化数据流
  std::stringstream iss(data);

  // Boost 序列化对象创建
  boost::archive::text_iarchive ia(iss);

  BoostPersistRaftNode boostPersistRaftNode;  // 创建一个 BoostPersistRaftNode 对象 boostPersistRaftNode，用于存储从 ia 中反序列化得到的数据。
  ia >> boostPersistRaftNode;   // 将输入流 ia 中的数据反序列化到 boostPersistRaftNode 对象中。

  // 恢复节点状态
  m_currentTerm = boostPersistRaftNode.m_currentTerm;
  m_votedFor = boostPersistRaftNode.m_votedFor;
  m_lastSnapshotIncludeIndex = boostPersistRaftNode.m_lastSnapshotIncludeIndex;
  m_lastSnapshotIncludeTerm = boostPersistRaftNode.m_lastSnapshotIncludeTerm;

  // 恢复日志列表
  m_logs.clear();
  for (auto& item : boostPersistRaftNode.m_logs) {
    raftRpcProctoc::LogEntry logEntry;
    logEntry.ParseFromString(item);   // item被序列化成了字符串，再解析回来
    m_logs.emplace_back(logEntry);
  }
}

/*** ------------------------------------------------ 快照 ---------------------------------------------------- ***/

/*
Snapshot 函数
主要功能：Raft 协议中根据快照信息更新节点的日志条目并持久化快照的功能（将状态机的状态快照持久化）
输入参数：
    index：这是要创建快照的日志索引。表示到此索引为止的所有日志条目将被包含在快照中。
    snapshot：这是用来建立快照的数据，通常是一个二进制字符串，包含了应用状态机的状态。
*/
void Raft::Snapshot(int index, std::string snapshot) {
  std::lock_guard<std::mutex> lg(m_mtx);

  // 检查快照索引的有效性
  if (m_lastSnapshotIncludeIndex >= index || index > m_commitIndex) { // 索引及之前的日志条目已经被建立快照了，或者索引日志条目还没有被提交。不创建快照
    DPrintf(
        "[func-Snapshot-rf{%d}] rejects replacing log with snapshotIndex %d as current snapshotIndex %d is larger or "
        "smaller ",
        m_me, index, m_lastSnapshotIncludeIndex);
    return;
  }

  auto lastLogIndex = getLastLogIndex();  // 获取当前日志的最后一个索引，用于后续检查和断言。

  // 创建新的快照所包含的索引和term
  int newLastSnapshotIncludeIndex = index;
  int newLastSnapshotIncludeTerm = m_logs[getSlicesIndexFromLogIndex(index)].logterm();
  
  std::vector<raftRpcProctoc::LogEntry> trunckedLogs;   // 日志向量，保存快照后面剩余的日志（分界点以后的，没有被创建为快照）
  for (int i = index + 1; i <= getLastLogIndex(); i++) {
    trunckedLogs.push_back(m_logs[getSlicesIndexFromLogIndex(i)]);
  }

  // 更新快照所包含的索引和term
  m_lastSnapshotIncludeIndex = newLastSnapshotIncludeIndex;
  m_lastSnapshotIncludeTerm = newLastSnapshotIncludeTerm;

  // 删除被创建快照的日志，替换为剩下的
  m_logs = trunckedLogs;

  // 更新提交索引和应用索引
  m_commitIndex = std::max(m_commitIndex, index);
  m_lastApplied = std::max(m_lastApplied, index);

  // 持久化当前节点的状态，和快照数据
  m_persister->Save(persistData(), snapshot);

  DPrintf("[SnapShot]Server %d snapshot snapshot index {%d}, term {%d}, loglen {%d}", m_me, index,
          m_lastSnapshotIncludeTerm, m_logs.size());
  
  // 断言检查，以确保截取后的日志长度加上快照包含索引等于原日志的最后索引
  myAssert(m_logs.size() + m_lastSnapshotIncludeIndex == lastLogIndex,
           format("len(rf.logs){%d} + rf.lastSnapshotIncludeIndex{%d} != lastLogjInde{%d}", m_logs.size(),
                  m_lastSnapshotIncludeIndex, lastLogIndex));
}

/*** -------------------------------------------- raft节点的初始化和启动 ---------------------------------------------------- ***/

/*
init 初始化函数
主要功能：设置了节点的初始状态、读取持久化状态，并启动了必要的协程和线程来执行 Raft 协议的核心逻辑
          服务或测试人员想要创建一个 Raft 服务器。
          所有 Raft 服务器（包括这个）的通信接口RaftRpcUtil都在 peers[] 中。这个服务器的接口是 peers[me]。所有服务器的 peers[] 数组具有相同的顺序。
          persister 是此服务器保存其持久状态的地方，并且最初还保存最近保存的状态（如果有）。
          applyCh 是测试人员或服务期望 Raft 发送 ApplyMsg 消息的通道。
          本函数必须快速返回，因此它应该为任何长时间运行的工作启动 goroutines。
*/
void Raft::init(std::vector<std::shared_ptr<RaftRpcUtil>> peers, int me, std::shared_ptr<Persister> persister, 
                std::shared_ptr<LockQueue<ApplyMsg>> applyCh) {
  m_peers = peers;
  m_persister = persister;
  m_me = me;

  m_mtx.lock();   // 加锁

  // 初始化 applyChan
  this->applyChan = applyCh;

  // 初始化当前raft节点的状态
  m_currentTerm = 0;      // 当前任期初始化为0
  m_status = Follower;    // 初始状态设为 Follower
  m_commitIndex = 0;      // 已提交索引初始化为0
  m_lastApplied = 0; // 最后应用的日志索引初始化为0
  m_logs.clear(); // 清空日志
  for (int i = 0; i < m_peers.size(); i++) {
    m_matchIndex.push_back(0); // 初始化匹配索引
    m_nextIndex.push_back(0); // 初始化下一个日志索引
  }
  m_votedFor = -1; // 初始化为未投票

  m_lastSnapshotIncludeIndex = 0; // 最后包含在快照中的日志索引初始化为0
  m_lastSnapshotIncludeTerm = 0; // 最后包含在快照中的日志任期初始化为0
  m_lastResetElectionTime = now(); // 初始化选举超时时间
  m_lastResetHearBeatTime = now(); // 初始化心跳超时时间

  // 如果存在持久化的内容，从其中恢复
  readPersist(m_persister->ReadRaftState());

  if (m_lastSnapshotIncludeIndex > 0) { // 有持久化数据，得到了恢复
    m_lastApplied = m_lastSnapshotIncludeIndex; // 如果有快照，则设置最后应用索引为快照索引
  }

  DPrintf("[Init&ReInit] Sever %d, term %d, lastSnapshotIncludeIndex {%d} , lastSnapshotIncludeTerm {%d}", m_me,
          m_currentTerm, m_lastSnapshotIncludeIndex, m_lastSnapshotIncludeTerm);

  m_mtx.unlock(); // 解锁

  // 启动各个定时器（通过协程调度和线程的方式）
  m_ioManager = std::make_unique<monsoon::IOManager>(FIBER_THREAD_NUM, FIBER_USE_CALLER_THREAD);

  // 启动三个循环定时器
  // todo:原来是启动了三个线程，现在是直接使用了协程，
  // 三个函数中leaderHearBeatTicker和electionTimeOutTicker执行时间是恒定的，所以直接用协程
  // pplierTicker时间受到数据库响应延迟和两次apply之间请求数量的影响，这个随着数据量增多可能不太合理，最好其还是启用一个线程。
  m_ioManager->scheduler([this]() -> void { this->leaderHearBeatTicker(); });
  m_ioManager->scheduler([this]() -> void { this->electionTimeOutTicker(); });

  std::thread t3(&Raft::applierTicker, this);
  t3.detach();
}

/*
start 函数
主要功能：领导者节点（Leader）处理客户端的命令请求并启动日志条目
输入参数：
    Op command: 客户端请求的命令。
    int* newLogIndex: 输出参数，表示新日志条目的索引。
    int* newLogTerm: 输出参数，表示新日志条目的任期。
    bool* isLeader: 输出参数，表示当前节点是否为领导者。
*/
void Raft::Start(Op command, int* newLogIndex, int* newLogTerm, bool* isLeader) {
  std::lock_guard<std::mutex> lg1(m_mtx);  // 加锁

  // 判断节点是否为Leader，只有Leader才会接收客户端的命令
  if (m_status != Leader) {
    DPrintf("[func-Start-rf{%d}]  is not leader");
    *newLogIndex = -1;
    *newLogTerm = -1;
    *isLeader = false;
    return;
  }

  // 根据命令创建新的日志条目
  raftRpcProctoc::LogEntry newLogEntry;
  newLogEntry.set_command(command.asString());    // 命令
  newLogEntry.set_logterm(m_currentTerm);     // term
  newLogEntry.set_logindex(getNewCommandIndex());    // index

  m_logs.emplace_back(newLogEntry);  // 加入到日志条目数组中

  int lastLogIndex = getLastLogIndex();   // 最新的日志index

  // leader应该不停的向各个Follower发送AE来维护心跳和保持日志同步，目前的做法是新的命令来了不会直接执行，而是等待leader的心跳触发
  DPrintf("[func-Start-rf{%d}]  lastLogIndex:%d,command:%s\n", m_me, lastLogIndex, &command);

  persist();
  *newLogIndex = newLogEntry.logindex();
  *newLogTerm = newLogEntry.logterm();
  *isLeader = true;
}



/*** -------------------------------------------- 一些重要的常用函数 ---------------------------------------------------- ***/

/*
getLastLogIndexAndTerm 函数
主要功能：取最新的log的logindex 和 log term
*/
void Raft::getLastLogIndexAndTerm(int* lastLogIndex, int* lastLogTerm) {
  if (m_logs.empty()) {  // 日志是空的，那直接就去找快照的最大索引和term
    *lastLogIndex = m_lastSnapshotIncludeIndex;  
    *lastLogTerm = m_lastSnapshotIncludeTerm;
    return;
  } else {  // 否则，就找日志中最后面的
    *lastLogIndex = m_logs[m_logs.size() - 1].logindex();
    *lastLogTerm = m_logs[m_logs.size() - 1].logterm();
    return;
  }
}


/*
getLastLogIndex 函数
主要功能：获取最新的log的logindex
*/
int Raft::getLastLogIndex() {
  int lastLogIndex = -1;
  int _ = -1;
  getLastLogIndexAndTerm(&lastLogIndex, &_);
  return lastLogIndex;
}

/*
getLastLogIndex 函数
主要功能：获取最新的log的log term
*/
int Raft::getLastLogTerm() {
  int _ = -1;
  int lastLogTerm = -1;
  getLastLogIndexAndTerm(&_, &lastLogTerm);
  return lastLogTerm;
}


/*
getNewCommandIndex 函数
主要功能：获取客户端新命令应该分配的Log Index
*/
int Raft::getNewCommandIndex() {
  auto lastLogIndex = getLastLogIndex();
  return lastLogIndex + 1;
}

/*
getPrevLogInfo 函数
主要功能：获取即将发送的 AppendEntries RPC 的前一个日志条目的索引和任期（preLogIndex 和 preLogTerm）Raft 协议中，leader 在发送 AppendEntries RPC 给 follower 时，需要带上前一个日志条目的信息（preLogIndex 和 preLogTerm）以便 follower 验证日志的一致性。
*/
void Raft::getPrevLogInfo(int server, int* preIndex, int* preTerm) {
  if (m_nextIndex[server] == m_lastSnapshotIncludeIndex + 1) {  // 要发送的日志正好是第一个日志（要么前面没有，要么前面被转换成快照了）
    *preIndex = m_lastSnapshotIncludeIndex;
    *preTerm = m_lastSnapshotIncludeTerm;
    return;
  }

  auto nextIndex = m_nextIndex[server];
  *preIndex = nextIndex - 1;
  *preTerm = m_logs[getSlicesIndexFromLogIndex(*preIndex)].logterm();
}

/*
GetState 函数
主要功能：获取当前raft节点的任期term和是否是leader
*/
void Raft::GetState(int *term, bool *isLeader) {
  m_mtx.lock();
  DEFER {
    m_mtx.unlock();
  };

  *term = m_currentTerm;
  *isLeader = (m_status == Leader);
}

/*
leaderUpdateCommitIndex 函数
主要功能：在 Raft 协议中更新 Leader 节点的提交索引（m_commitIndex）。提交索引表示已提交的最大日志条目索引，Leader 在确保大多数节点复制了该条目之后更新该索引。
*/
void Raft::leaderUpdateCommitIndex() {
  m_commitIndex = m_lastSnapshotIncludeIndex;   // 快照化的一定是提交了的

  // 从最后一个日志条目向前检查
  for (int index = getLastLogIndex(); index >= m_lastSnapshotIncludeIndex + 1; index--) {
    int sum = 0;

    // 统计有多少节点的m_matchIndex大于等于当前的index
    for (int i = 0; i < m_peers.size(); i++) {
      if (i == m_me) {
        sum += 1;  // 包括自己
        continue;
      }
      if (m_matchIndex[i] >= index) {
        sum += 1;
      }
    }

    // 如果超过了半数，并且日志条目的任期为当前任期，说明这个index是在这个Leader下被提交的
    if (sum >= m_peers.size() / 2 + 1 && getLogTermFromLogIndex(index) == m_currentTerm) {
      m_commitIndex = index;
      break;   // 找到了，则前面的一定都是提交了的，直接结束
    }
  }
}

/*
getLogTermFromLogIndex 函数
主要功能：根据日志条目的index，获取日志条目的term
*/
int Raft::getLogTermFromLogIndex(int logIndex) {
  // 必须是在建立快照以后的日志
  myAssert(logIndex >= m_lastSnapshotIncludeIndex,
           format("[func-getSlicesIndexFromLogIndex-rf{%d}]  index{%d} < rf.lastSnapshotIncludeIndex{%d}", m_me,
                  logIndex, m_lastSnapshotIncludeIndex));

  int lastLogIndex = getLastLogIndex();

  // 确保要查询的日志条目的index正确
  myAssert(logIndex <= lastLogIndex, format("[func-getSlicesIndexFromLogIndex-rf{%d}]  logIndex{%d} > lastLogIndex{%d}",
                                            m_me, logIndex, lastLogIndex));
  
  if (logIndex == m_lastSnapshotIncludeIndex) {
    return m_lastSnapshotIncludeTerm;
  } else {
    return m_logs[getSlicesIndexFromLogIndex(logIndex)].logterm();
  }
}

/*
getSlicesIndexFromLogIndex 函数
主要功能；根据log index获得这个日志条目log在m_logs数组中的索引（由逻辑索引->物理索引）
*/
int Raft::getSlicesIndexFromLogIndex(int logIndex) {
  // 找到index对应的真实下标位置！！！
  // 限制，输入的logIndex必须保存在当前的logs里面（不包含snapshot）
  myAssert(logIndex > m_lastSnapshotIncludeIndex,
           format("[func-getSlicesIndexFromLogIndex-rf{%d}]  index{%d} <= rf.lastSnapshotIncludeIndex{%d}", m_me,
                  logIndex, m_lastSnapshotIncludeIndex));

  int lastLogIndex = getLastLogIndex();
  // 确保要查找的日志条目索引正确
  myAssert(logIndex <= lastLogIndex, format("[func-getSlicesIndexFromLogIndex-rf{%d}]  logIndex{%d} > lastLogIndex{%d}",
                                            m_me, logIndex, lastLogIndex));
  
  int SliceIndex = logIndex - m_lastSnapshotIncludeIndex - 1;  // m_lastSnapshotIncludeIndex相当于-1，所以要多-1
  return SliceIndex;
}


/*
matchLog 函数
主要功能：判断输入的logIndex所对应的log的任期是不是logterm
*/
bool Raft::matchLog(int logIndex, int logTerm) {
  // 保证logIndex是存在的，即≥rf.lastSnapshotIncludeIndex	，而且小于等于rf.getLastLogIndex()
  myAssert(logIndex >= m_lastSnapshotIncludeIndex && logIndex <= getLastLogIndex(),
           format("不满足: logIndex{%d}>=rf.lastSnapshotIncludeIndex{%d}&&logIndex{%d}<=rf.getLastLogIndex{%d}",
                  logIndex, m_lastSnapshotIncludeIndex, logIndex, getLastLogIndex()));
  return logTerm == getLogTermFromLogIndex(logIndex);
}




//
// Persister类声明，用于管理Raft协议中状态和快照的持久化存储
// created by magic_pri on 2024-7-9
//

#ifndef SKIP_LIST_ON_RAFT_PERSISTER_H
#define SKIP_LIST_ON_RAFT_PERSISTER_H

#include <fstream>
#include <mutex>
#include <string>

/*
Persister 类的主要功能是管理 Raft 协议中的状态和快照文件。通过互斥锁保证线程安全地访问共享资源，并提供保存和读取状态和快照的功能。
同时，类还管理文件输出流和文件大小，以提高性能。
*/
class Persister {
public:
  void Save(std::string raftstate, std::string snapshot);   // 保存 Raft 状态和快照
  std::string ReadSnapshot();   // 读取快照
  void SaveRaftState(const std::string& data);    // 保存 Raft 状态
  long long RaftStateSize();    // 获取Raft 状态的大小
  std::string ReadRaftState();    // 读取 Raft 状态
  explicit Persister(int me);   // 构造函数，接受一个整型参数 me（用于区分不同的实例）。加入explicit，【禁止隐式类型转换、禁止隐式调用拷贝构造函数】
  ~Persister();

private:
  void clearRaftState();    // 清除 Raft 状态
  void clearSnapshot();     // 清除快照
  void clearRaftStateAndSnapshot();   // 清除 Raft 状态和快照

private:
  std::mutex m_mtx;       // 互斥锁
  std::string m_raftState;    // 保存 Raft 状态的字符串
  std::string m_snapshot;     // 保存快照的字符串
  const std::string m_raftStateFileName;  // Raft 状态文件的名称
  const std::string m_snapshotFileName;   // 快照文件的名称
  std::ofstream m_raftStateOutStream;     // 输出流，用于保存 Raft 状态
  std::ofstream m_snapshotOutStream;      // 输出流，用于保存快照
  long long m_raftStateSize;    // 保存 Raft 状态的大小，以避免每次读取文件来获取大小
};

#endif
//
// Persister类的具体实现
// created by magic_pri on 2024-7-9
//

#include "Persister.h"
#include "util.h"


/*
Save 函数
主要功能：将传入的 raftstate 和 snapshot 保存到本地文件（持久化）
TODO：会涉及反复打开文件的操作，没有考虑如果文件出现问题会怎么办？？
*/
void Persister::Save(std::string raftstate, std::string snapshot) {
  std::lock_guard<std::mutex> lg(m_mtx);   // 加互斥锁
  clearRaftStateAndSnapshot();    // 清除之前保存的 Raft 状态和快照
  // 将raftstate和snapshot写入本地文件
  m_raftStateOutStream << raftstate;
  m_snapshotOutStream << snapshot;
  m_raftStateSize += raftstate.size();
}

/*
ReadSnapshot 函数
主要功能：读取并返回保存的快照
*/
std::string Persister::ReadSnapshot() {
  std::lock_guard<std::mutex> lg(m_mtx);
  if (m_snapshotOutStream.is_open()) {
    m_raftStateOutStream.close();   // 把输出流关闭，避免冲突，保证文件可读。同时可以刷新缓冲区，确保数据一致性
  }

  DEFER { // 当本函数执行结束后，自动调用此函数，重新打开输出流，默认为追加写
    m_snapshotOutStream.open(m_snapshotFileName);  
  };

  std::fstream ifs(m_snapshotFileName, std::ios_base::in);  // 创建文件流对象，以只读std::ios_base::in的方式打开快照文件
  if (!ifs.good()) {  // 未成功打开
    return "";
  }
  // 成功打开
  std::string snapshot;
  ifs >> snapshot;
  ifs.close();  // 关闭文件流
  return snapshot;
}

/*
SaveRaftState 函数
主要功能：保存 Raft 状态数据（持久化）
*/
void Persister::SaveRaftState(const std::string &data) {
  std::lock_guard<std::mutex> lg(m_mtx);  // 加锁
  clearRaftState();
  m_raftStateOutStream << data;
  m_raftStateSize += data.size();
}

/*
RaftStateSize 函数
主要功能：获取Raft状态数据的大小
*/
long long Persister::RaftStateSize() {
  std::lock_guard<std::mutex> lg(m_mtx);  // 加锁
  return m_raftStateSize;
}

/*
ReadRaftState 函数
主要功能：读取并返回保存的 Raft 状态
*/
std::string Persister::ReadRaftState() {
  std::lock_guard<std::mutex> lg(m_mtx);

  std::fstream ifs(m_raftStateFileName, std::ios_base::in);
  if (!ifs.good()) {
    return "";
  }
  std::string snapshot;
  ifs >> snapshot;
  ifs.close();
  return snapshot;
}

/*
构造函数
主要功能：初始化文件状态、绑定文件输出流
*/
Persister::Persister(const int me) : m_raftStateFileName("raftstatePersist" + std::to_string(me) + ".txt"),   // 初始化文件名称
                                     m_snapshotFileName("snapshotPersist" + std::to_string(me) + ".txt"),
                                     m_raftStateSize(0) {
  // 检查文件是否可以打开，并清空文件
  bool fileOpenFlag = true;   // 文件可以打开的标志
  std::fstream file(m_raftStateFileName, std::ios::out | std::ios::trunc);   // 创建文件流对象打开raft状态文件，同时清空（std::ios::out | std::ios::trunc模式）
  if (file.is_open()) {
    file.close();   // 能正常打开，再将其关闭后面再用
  } else {
    // 无法打开，将flag置为false，表示出错
    fileOpenFlag = false;
  }
  file = std::fstream(m_snapshotFileName, std::ios::out | std::ios::trunc);
  if (file.is_open()) {
    file.close();
  } else {
    fileOpenFlag = false;
  }
  if (!fileOpenFlag) {
    DPrintf("[func-Persister::Persister] file open error");
  }

  // 绑定流
  m_raftStateOutStream.open(m_raftStateFileName);
  m_snapshotOutStream.open(m_snapshotFileName);
}

/*
析构函数
主要功能：对象销毁时，确保输出流被关闭
*/
Persister::~Persister() {
  if (m_raftStateOutStream.is_open()) {
    m_raftStateOutStream.close();
  }
  if (m_snapshotOutStream.is_open()) {
    m_snapshotOutStream.close();
  }
}

/*
clearRaftState 
主要功能：清空Raft状态
*/
void Persister::clearRaftState() {
  m_raftStateSize = 0;  // 大小重置为0
  // 关闭文件流
  if (m_raftStateOutStream.is_open()) {
    m_raftStateOutStream.close();
  }
  // 重新打开文件流并清空文件内容
  m_raftStateOutStream.open(m_raftStateFileName, std::ios::out | std::ios::trunc);
}

/*
clearSnapshot 
主要功能：清空快照
*/
void Persister::clearSnapshot() {
  if (m_snapshotOutStream.is_open()) {
    m_snapshotOutStream.close();
  }
  m_snapshotOutStream.open(m_snapshotFileName, std::ios::out | std::ios::trunc);
}

/*
clearRaftStateAndSnapshot
主要功能：清空Raft状态和快照
*/
void Persister::clearRaftStateAndSnapshot() {
  clearRaftState();
  clearSnapshot();
}
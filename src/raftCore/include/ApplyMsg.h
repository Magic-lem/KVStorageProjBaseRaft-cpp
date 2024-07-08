//
//  ApplyMsg类，它是在 Raft 协议中传递应用到状态机的消息
//  created by magic_pri on 2024-7-8
//

#ifndef APPLYMSG_H
#define APPLYMSG_H

/*
ApplyMsg 类用于在 Raft 协议中传递需要应用到状态机的消息。
它包含了命令和快照相关的信息，并通过构造函数对所有成员变量进行初始化，确保初始状态的有效性和一致性。
*/
class ApplyMsg {
public:
  //两个valid最开始要赋予false！！
  ApplyMsg() : CommandValid(false), Command(), CommandIndex(-1), SnapshotValid(false), SnapshotTerm(-1), SnapshotIndex(-1){}

public:
  bool CommandValid;    // 表示 Command 是否有效。
  std::string Command;    // 表示需要应用到状态机的命令
  int CommandIndex;    // 该命令在日志中的索引
  bool SnapshotValid;       // Snapshot是否有效
  std::string Snapshot;   // 快照数据
  int SnapshotTerm;       // 快照的最后一个日志条目的任期号
  int SnapshotIndex;      // 快照的最后一个日志条目的索引
};

#endif
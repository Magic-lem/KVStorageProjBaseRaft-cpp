//
// 客户端与kv server交互的定义和声明
// created by magic_pri on 2024-7-17
//

#ifndef SKIP_LIST_ON_RAFT_CLERK_H
#define SKIP_LIST_ON_RAFT_CLERK_H

#include <string>
#include <vector>
#include "raftServerRpcUtil.h"
#include "util.h"
#include "mprpcconfig.h"

// 客户端与kv server交互类
// 封装了与多个 Raft 节点通信的逻辑，实现了基本的 Get、Put 和 Append 操作
class Clerk {
public:
  //对外暴露的三个功能和初始化函数
  void Init(std::string configFileName);
  std::string Get(std::string key);

  void Put(std::string key, std::string value);
  void Append(std::string key, std::string value);

public:
  Clerk();  // 构造函数

private:
  // 生成一个随机的客户端标识符 clientId。
  std::string Uuid() {
    return std::to_string(rand()) + std::to_string(rand()) + std::to_string(rand()) + std::to_string(rand());
  }

  // 辅助函数，用于发送 Put 或 Append 请求
  void PutAppend(std::string key, std::string value, std::string op);

private:
  std::vector<std::shared_ptr<raftServerRpcUtil>> m_servers;    // 保存与所有kvserver raft节点的通信接口
  std::string m_clientId;   // 当前客户端的标识符ID
  int m_requestId;    // 请求的ID，逐渐累加
  int m_recentLeaderId;   // 最近一次已知的 Raft leader 节点的编号
};


#endif
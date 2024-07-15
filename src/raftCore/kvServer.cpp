//
// KV数据库类方法的具体实现
// created by magic_pri on 2024-7-14
//

#include "kvServer.h"
#include "config.h"
#include "../common/include/util.h"

/*
DprintfKVDB 函数
主要功能：打印键值数据库的内容
*/
void KvServer::DprintfKVDB() {
  if (!Debug) return;

  std::lock_guard<std::mutex> lg(m_mtx);

  DEFER {
    m_skipList.display_list();
  };
}

/*
ExecuteAppendOpOnKVDB 函数
主要功能：执行append功能，添加键值对
*/
void KvServer::ExecuteAppendOpOnKVDB(Op op) {
  
}
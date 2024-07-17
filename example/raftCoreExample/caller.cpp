//
// 客户端的主函数实现示例
// created by magic_pri on 2024-7-17
//

#include "clerk.h"
#include "util.h"
#include <iostream>

int main() {
  Clerk client;   // 客户端对象
  client.Init("test.conf");   // 初始化客户端，建立与所有kvserver raft节点的RPC连接

  auto start = now();
  int count = 500;
  int temp = count;

  while (temp--) {
    client.Put("x", std::to_string(temp));    // 向数据添加数据，执行Put函数
    std::string get1 = client.Get("x");       // 访问数据库数据，执行Get寒素
    std::printf("get return :{%s}\r\n", get1.c_str());
  }
  return 0;
}
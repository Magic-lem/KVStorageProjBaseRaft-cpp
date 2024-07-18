//
// raft算法程序服务端的主函数示例，创建和启动多个RaftKV节点，并将这些节点的信息写入一个配置文件
// created by magic_pri on 2024-7-17
//
#include <iostream>
#include "raft.h"
#include "kvServer.h"
// #include <kvServer.h>
#include <unistd.h>
#include <iostream>
#include <random>


void ShowArgsHelp();

/*
主函数
输入参数：命令行参数
    argc (argument count)：表示传递给程序的命令行参数的数量，包括程序名称本身
    argv (argument vector)：字符指针数组（数组的每个元素都是一个指向字符数组的指针），存储传递给程序的命令行参数
*/
int main(int argc, char **argv) {
  // 读取命令行参数
  if (argc < 2) {   // 命令行参数少于两个，缺少参数，报错
    ShowArgsHelp();
    exit(EXIT_FAILURE);
  }

  std::random_device rd;    // 随机数种子生成器
  std::mt19937 gen(rd());    // 梅森旋转随机数生成器，使用 rd() 生成的种子进行初始化。gen为创建的伪随机数生成器实例，用于产生随机数。
  std::uniform_int_distribution<> dis(10000, 29999);       // 均匀分布的整数随机数分布范围对象，生成 10000 到 29999 之间的随机数，用于产生在这个范围内均匀分布的随机端口号
  unsigned short StartPort = dis(gen);  // 生成一个 10000 到 29999 之间的端口号，用于为第一个 Raft 节点指定起始端口号，后续节点的端口号基于这个起始端口号递增。


  int c = 0;     // 用于存储 getopt 函数返回的选项字符
  int nodeNum = 0;      // Raft 节点数量
  std::string configFileName;   // 配置文件名称


  // 使用 getopt 函数解析命令行参数，支持的选项为 -n 和 -f
  while ((c = getopt(argc, argv, "n:f:")) != -1) {
    switch (c) {
      case 'n':
        nodeNum = atoi(optarg);     // 解析 -n 选项，将 optarg 转换为整数并赋值给 nodeNum
        break;
      case 'f':
        configFileName = optarg;    // 解析 -f 选项，将 optarg 赋值给 configFileName
        break;
      default:
        ShowArgsHelp();     // 选项不匹配，显示帮助信息
        exit(EXIT_FAILURE);
    }
  }

  // 使用追加写的方式打开配置文件，如果文件不存在则创建文件
  std::ofstream file(configFileName, std::ios::out | std::ios::app);
  file.close();  // 关闭文件流

  // 使用截断模式打开配置文件，打开文件并清空其内容 （为什么要打开两次？）
  file = std::ofstream(configFileName, std::ios::out | std::ios::trunc);
  if (file.is_open()) {
    file.close();
    std::cout << configFileName << " 已清空" << std::endl;
  } else {
    std::cout << "无法打开 " << configFileName << std::endl;
    exit(EXIT_FAILURE);
  }

  // 根据节点数量创建多个kvserver raft节点
  for (int i = 0; i < nodeNum; i++) {
    short port = StartPort + static_cast<short>(i);   // 为每个节点分配一个唯一的端口号
    std::cout << "start to create raftkv node: " << i << "  port: " << port << " pid: " << getpid() << std::endl;

    pid_t pid = fork();   // 创建子进程
    if (pid == 0) {   
      // pid为0，说明本进程是子进程
      auto kvServer = new KvServer(i, 500, configFileName, port);   // 创建 kvserver 实例
      pause();    // 子进程进入等待状态，直到接收到信号
    } else if (pid > 0) {
      // pid > 0，说明本进程是父进程
      sleep(1);   // 等待1s，确保子进程成功启动
    } else {
      // 进程创建失败
      std::cerr << "Failed to create child process." << std::endl;  // 输出错误消息
      exit(EXIT_FAILURE);  // 退出程序
    }
  }

  pause();    // 主进程进入等待状态，不会执行 return 语句
  return 0; 
}


void ShowArgsHelp() { std::cout << "format: command -n <nodeNum> -f <configFileName>" << std::endl; }
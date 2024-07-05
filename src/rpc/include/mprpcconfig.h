//
// RPC配置类，用于加载和查询配置文件中的配置项信息
// created by magic_pri on 2024-7-5
//
#ifndef MPRPCCONFIG_H
#define MPRPCCONFIG_H

#include <string>
#include <unordered_map>



/* 配置文件示例
        # 这是注释行
        rpcserverip = 127.0.0.1
        rpcserverport = 8080
        zookeeperip = 192.168.1.100
        zookeeperport = 2181

在RPC（Remote Procedure Call，远程过程调用）中，这些配置信息通常代表着用于建立和管理RPC连接的关键参数和设置。具体来说，示例配置文件中的每个配置项可能表示以下内容：
        rpcserverip: RPC 服务器的 IP 地址。这是客户端在进行远程调用时需要连接的目标服务器的地址。
        rpcserverport: RPC 服务器的端口号。指定了 RPC 服务在服务器上监听客户端连接的端口。
        zookeeperip: 可选项，用于服务发现的 Zookeeper 服务器的 IP 地址。在某些分布式系统中，Zookeeper 用于管理和发现可用的服务节点。
        zookeeperport: 可选项，Zookeeper 服务器的端口号。用于连接到 Zookeeper 服务器以获取服务节点的信息。
*/


// 框架读取配置文件类
class MprpcConfig {
public:
  void LoadConfigFile(const char *config_file);   // 负责解析加载配置文件
  std::string Load(const std::string &key);   // 查询指定键名 key 对应的配置项值

private:
  std::unordered_map<std::string, std::string> m_configMap;   // 存储配置文件中的键值对信息
  void Trim(std::string &src_buf);    // 去除字符串 src_buf 前后的空格
};



#endif
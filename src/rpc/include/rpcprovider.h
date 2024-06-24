//
// RPC框架的服务端组件实现，负责服务注册与启动、连接处理、调用服务方法并将结果返回给客户端
// created by magic_pri on 2024-6-19
//

#ifndef RPCPROVIDER_H
#define RPCPROVIDER_H

#include <muduo/net/EventLoop.h>
#include <muduo/net/TcpServer.h>
#include <muduo/net/InetAddress.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/service.h>
#include <unordered_map>
#include <string>

// @brief 自实现的一个专门发布rpc服务的网络对象类，基于Muduo网络库和Protobuf的RPC框架的服务端
// TODO: rpc客户端变成长连接，所以rpc服务端最好能提供一个定时器，用以断开很久没有请求的连接
// TODO: rpc客户端也需要在每次发送之前检查连接状态
class RpcProvider {
public:
    // 服务端提供给外部使用的接口
    void NotifyService(google::protobuf::Service *service);  // 注册一个RPC服务

    void Run(int nodeIndex, short port);  // 启动RPC服务节点，开始提供RPC远程网络调用服务

    ~RpcProvider();  // 析构函数

private:
    muduo::net::EventLoop m_eventLoop;   // 时间循环对象，负责管理事件和调度回调函数
    std::shared_ptr<muduo::net::TcpServer> m_muduo_server;   // TCP服务端对象，负责接受客户端连接和管理连接

    // service服务信息，保存RPC服务对象及其方法
    // 服务对象是指在服务端定义的具体服务对象，当接收到来自客户端的TCP请求时，'RpcProvicer'会调用这个服务对象来处理请求
    struct ServiceInfo {
        google::protobuf::Service *m_service;  // 指向服务对象的指针
        std::unordered_map<std::string, const google::protobuf::MethodDescriptor*> m_methodMap;   // 保存服务方法的映射， <方法名-方法描述符指针>
    };

    std::unordered_map<std::string, ServiceInfo> m_serviceMap;  // 存储注册成功的服务信息，<服务名-ServiceInfo对象>

    // 出现新socket连接的回调函数
    void onConnection(const muduo::net::TcpConnectionPtr &);

    // 读写消息回调函数
    void onMessage(const muduo::net::TcpConnectionPtr &, muduo::net::Buffer *, muduo::Timestamp);

    // Closure的回调函数，用于RPC方法调用的响应
    void SendRpcResponse(const muduo::net::TcpConnectionPtr &, google::protobuf::Message *);
};


#endif
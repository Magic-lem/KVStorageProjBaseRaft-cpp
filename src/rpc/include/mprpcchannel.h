//
// RPC通道类，负责发送和接收RPC调用
// created by magic_pri on 2024-6-20
//

#ifndef MPRPCCHANNEL_H
#define MPRPCCHANNEL_H

#include <google/protobuf/service.h>
#include <string>>

/*
googe::protobuf框架中提供的RpcChannel，抽象了底层的网络通信细节，客户端可以通过RpcChannel
来发送请求和接受响应，而不需要关心具体的网络通信实现
主要作用：
    1. 发送请求：将客户端的RPC方法调用转换成网络请求，并发送到服务器
    2. 接收响应：从服务端接收响应并传递给客户端
*/
class MprpcChannel: public google::protobuf::RpcChannel {
public:
    MprpcChannel(std::string ip, short port, bool connectNow);  // 构造函数

    // 重写CallMethod纯虚函数。该函数是用于RPC方法调用的数据序列化和网络发送，所有代理对象调用的RPC方法都会调用此函数
    void CallMethod(const google::protobuf::MethodDescriptor *method, google::protobuf::RpcController *controller,
                    const google::protobuf::Message *request, google::protobuf::Message *response, 
                    google::protobuf::Closure *done) override;

private:
    int m_clientFd; // 保存客户端文件描述符，用于网络通信
    const std::string m_ip;  // 保存IP地址
    const uint16_t m_port;   // 端口号

    // 尝试连接到指定IP和端口，并设置m_clientFd。成功返回true，否则返回false
    bool newConnect(const char *ip, uint16_t port, std::string *errMsg);
};


#endif
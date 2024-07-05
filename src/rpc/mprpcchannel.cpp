//
// RpcChannel派生类mprpcchannel的实现
// created by magic_pri on 2024-6-20
// 
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "./include/mprpcchannel.h"
#include "./include/mprpccontroller.h"
#include "./include/rpcheader.pb.h"
#include "common/include/util.h"

// 所有通过代理对象调用的rpc方法，都会到这里
// 统一通过rpcChannel来调用服务方法
// 通体进行rpc方法调用的数据序列化和网络发送

/*
构造函数
目的：初始化一个RPC Channel对象
输入参数：
    string ip：RPC服务器的IP地址。
    short port：RPC服务器的端口号。
    bool connectNow：是否立即连接到RPC服务器。
*/
MprpcChannel::MprpcChannel(string ip, short port, bool connectNow) : m_ip(ip), m_port(port), m_clientFd(-1) {
  // 使用tcp编程，完成rpc方法的远程调用，使用的是短连接，因此通信结束后即断开连接，每次都要重新连接上去
  // TODO: 待改成长连接。
  if (!connectNow) {
    return;   // 延迟连接
  }

  // 否则，当前建立连接
  std::string errMsg;
  auto rt = newConnect(ip.c_str(), port, &errMsg);
  int tryCount = 3;  
  while (!rt && tryCount--) {      // 如果连接失败，则重试3次
    std::cout << errMsg << std::endl;
    rt = newConnect(ip.c_str(), port, &errMsg);
  }
}

/*
CallMethod 重写方法
目的：负责进行RPC方法调用的数据序列化和网络发送，接收响应并进行反序列化
输入参数：
    method：关于要调用的方法的描述符
    controller: 用于管理RPC调用的控制器
    request: 要发送的请求消息
    response：用于存储接收的响应消息
    done：RPC方法调用的回调函数
步骤：
    1. 连接检查和重新连接
    2. 获取服务和方法名称
    3. 参数序列化
    4. 构建RPC头
    5. 构建发送的数据流
    6. 发送RPC请求
    7. 接收RPC响应
    8. 反序列化响应数据
*/
void MprpcChannel::CallMethod(const google::protobuf::MethodDescriptor *method, google::protobuf::RpcController* controller,
                              const google::protobuf::Message *request, google::protobuf::Message *response,
                              google::protobuf::Closure *done) {
    // 检查连接状态，如何连接断开，则重新连接
    if (m_clientFd == -1) {  // 文件描述符为-1，说明文件描述符无效或为打开，即未连接或连接已经断开
        std::string errMsg;
        bool rt = newConnect(m_ip.c_str(), m_port, &errMsg);  // 尝试重连
        if (!rt){ // 连接失败
            DPrintf("[func-MprpcChannel::CallMethod]重连接ip: {%s} port {%d}失败", m_ip.c_str(), m_port);
            controller->SetFailed(errMsg);
        } else {
            DPrintf("[func-MprpcChannel::CallMethod]连接ip: {%s} port {%d}成功", m_ip.c_str(), m_port);
        }
    }

    // 获取方法描述符
    const google::protobuf::ServiceDescriptor *sd = method->se rvice();   // 服务描述信息
    std::string service_name = sd->name();   // 服务名称
    std::string method_name = method->name();   // 方法名称

    // 将请求消息进行序列化
    uint32_t args_size{};    // 初始化一个32位无符号整形，用于后面存储序列结果长度
    std::string args_str;    // 字符串，用于保存请求序列化结果
    if (request->SerializeToString(&args_str)) {   // 注意SerializeToString和SerializeAsString的区别
        args_size = args_str.length();
    } else {
        controller->SetFailed("serialize request error!");
        return;
    }

    // 构建RPC请求头
    RPC::RpcHeader rpcHeader;   // RPC头对象，自定义的Protobuf消息类型，利用.proto文件生成
    rpcHeader.set_service_name(service_name);
    rpcHeader.set_method_name(method_name);
    rpcHeader.set_args_size(args_size);

    // 将请求头序列化
    std::string rpc_header_str;
    if (!rpcHeader->SerializeToString(&rpc_header_str)) {
        controller->SetFailed("serialize rpc header error!");
        return;
    }

    // 构建发送数据流
    // 对请求头进行编码是由于请求头包含多元数据，可以通过编码实现更加结构化、紧凑的形式来表示和传输
    std::string send_rpc_str;
    {   // 通过一个大括号，来创建临时作用域，以控制局部对象的生命周期。在作用于结束后将对象销毁，资源释放。
        google::protobuf::io::StringOutputStream string_output(&send_rpc_str);  // 构建字符串发送数据流
        google::protobuf::io::CodedOutputStream coded_output(&string_output);   // 构建编码发送数据流
        // 数据流的构建过程：先使用CodedOutputStream'将元数据 (header的长度和内容)编码，然后使用StringOutputStream将编码内容写入字符串数据流中
        
        // 首先写入请求头的长度，使用变长编码的方式写入
        coded_output.WriteVarint32(static_cast<uint32_t>(rpc_header_str.size()));   
        // 写入请求头内容
        coded_output.WriteString(rpc_header_str);
    }
    // 将序列化的请求消息加在请求头编码的数据流后面，形成最终的发送数据流。
    send_rpc_str += args_str;     // 虽然是直接加在后面了，本质上是一个字符串数据流。头部只是多了个变长编码


    // 发送数据
    while (-1 == send(m_clientFd, send_rpc_str.c_str(), send_rpc_str.size(), 0)) {  // 调用hook函数send向RPC服务端发送数据，返回值为-1代表发送失败，触发重试逻辑
      // 错误处理
      char errtxt[512] = {0};
      sprintf(errtxt, "send error! errno:%d", errno);   // 使用sprintf函数将错误信息和errno值格式化到errtxt字符串中

      // 重新连接
      std::cout << "尝试重新连接, 对方ip: " << m_ip << " 对方端口: " << m_port << std::endl;
      close(m_clientFd);    // hook函数，关闭文件描述符
      m_clientFd = -1;
      std::string errMsg;
      bool rt = newConnect(m_ip.c_str(), m_port, &errMsg);  // 连接到指定ip:端口
      if (!rt) {
        controller->SetFailed(errMsg);
        return;
      }
    }

    // 接收rpc请求的响应
    /*
      当前的rpc为同步阻塞模式，即recv函数在没有接收到数据前会阻塞等待，直到从套接字接收到数据、发生错误或者连接关闭。
    */
    char recv_buf[1024] = {0};    // 接收缓冲区
    int recv_size = 0;  // 接受缓冲区大小
    if (-1 == (recv_size = recv(m_clientFd, recv_buf, 1024, 0))) {  // hook函数，从套接字m_clientFd接收数据到recv_buf，返回值-1代表接收失败
      close(m_clientFd);
      m_clientFd = -1;
      char errtxt[512] = {0};
      sprintf(errtxt, "ercv error! errno:%d", errno);
      controller->SetFailed(errtxt);
      return;
    }

    // 解析响应数据：反序列化，存到response中
    if (!response->ParseFromArray(recv_buf, recv_size)) {   // 若返回false，则表示解析失败
      char errtxt[1050] = {0};
      sprintf(errtxt, "parse error! response_str:%s", recv_buf);
      controller->SetFailed(errtxt);
      return;
    }

    // 解析成功，完成
}


/*
newConnect 函数
目的：尝试连接到远程RPC服务节点指定端口，并设置m_clientFd。成功返回true，否则返回false
输入参数：
    ip：远程RPC服务节点的IP地址
    port：端口号
    errMsg：用于存储错误信息的字符串指针
为什么ip使用chat*类型：
    许多网络编程接口和系统调用中，使用C风格的字符串（char* 或 const char*）作为参数是历史遗留问题。这些接口设计得比较早，在C语言中没有std::string这种类型，因此沿用了C风格的字符串。
*/
bool MprpcChannel::newConnect(const char* ip, uint16_t port, string* errMsg) {
  /*
  socket(AF_INET, SOCK_STREAM, 0)：创建一个TCP套接字。
      AF_INET：地址族，表示使用IPv4。
      SOCK_STREAM：套接字类型，表示使用TCP协议。
      0：协议，通常为0，表示自动选择合适的协议。
  */
  int clientfd = socket(AF_INET, SOCK_STREAM, 0);   // 返回套接字的文件描述符、
  int (-1 == clientfd) {
    char errtxt[512] = {0};
    sprintf(errtxt, "Create socket error! errno:%d", errno);
    m_clientFd = -1;
    *errMsg = errtxt;
    return false;
  }

  // 设置远程RPC服务节点地址
  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  server_addr.sin_addr.s_addr = inet_addr(ip);

  // 连接远程RPC服务节点
  if (-1 == connect(clientfd, (strcut sockaddr*)&server_addr, sizeof(server_addr))) {  // hook函数
    // 连接失败
    close(clientfd);
    cahr errtxt[512] = {0};
    sprintf(errtxt, "connect fail! errno:%d", errno);
    m_clientFd = -1;
    *errMsg = errtxt;
    return false;
  }

  // 连接成功
  m_clientFd = clientfd;
  return true;
}
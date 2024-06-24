//
// RpcChannel派生类mprpcchannel的实现
// created by magic_pri on 2024-6-20
// 

#include "./include/mprpcchannel.h"
#include "./include/mprpccontroller.h"
#include "./include/rpcheader.pb.h"
#include "common/include/util.h"

// 所有通过代理对象调用的rpc方法，都会到这里
// 统一通过rpcChannel来调用服务方法
// 通体进行rpc方法调用的数据序列化和网络发送

/*
构造函数
*/


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
    const google::protobuf::ServiceDescriptor *sd = method->service();   // 服务描述信息
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
    if (!request->SerializeToString(&rpc_header_str)) {
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
    send_rpc_str += args_str;   


    // 发送数据并处理发送失败
    while (-1 == send())
}
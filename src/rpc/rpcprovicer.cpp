//
// RPC框架服务端的具体实现
// 主要功能：接收客户端的RPC请求并进行处理，然后返回结果
// created by magic_pri on 2024-6-19
// 

#include "include/rpcprovider.h"
#include "include/rpcheader.pb.h"

#include <arpa/inet.h>    // inet_ntoa
#include <netdb.h>  // hostent struct
#include <unistd.h>  // gethostname, gethostbyname
#include <fstream>  // ofstream

/*
NotifyService 方法
目的：注册服务及其方法
步骤：
    1. 获取服务的描述信息
    2. 获取服务的名称和方法的数量
    3. 遍历方法，将每个方法的描述信息保存到‘service_info'的'm_methodMap'中
    4. 将服务对象保存到'service_info'中，并将'service_info'添加到'm_serviceMap'中
*/
void RpcProvider::NotifyService(google::protobuf::Service *service) {
    ServiceInfo service_info;

    // 获取服务对象描述信息
    const google::protobuf::ServiceDescriptor *pserviceDesc = service->GetDescriptor();

    // 获取服务对象的名称和方法数量
    std::string service_name = pserviceDesc->name();
    int methodCnt = pserviceDesc->method_count();

    // 保存每个方法信息
    for (int i = 0; i < methodCnt; i++){
        const google::protobuf::MethodDescriptor *pmethodDesc = pserviceDesc->method(i);
        std::string method_name = pmethodDesc->name();
        service_info.m_methodMap.insert(std::pair(method_name, pmethodDesc));
    }

    // 保存服务对象
    service_info.m_service = service;
    m_serviceMap.insert(std::pair(service_name, service_info));
}

/*
Run 方法
目的：启动RPC服务节点，开始提供RPC远程网络调用服务
步骤：
    1. 获取本机IP地址和端口号，并将其写入配置文件
    2. 创建‘TCPServer’对象，并设置连接回调和消息回调
    3. 设置Muduo库的线程数量
    4. 启动TCP服务器并进入事件循环
*/
void RpcProvider::Run(int nodeINdex, short port){
    // 获取本机IP地址
    char *ipC;
    char hname[128];
    struct hostent *hent;
    gethostname(hname, sizeof(hname));  // 获得当前主机的标准主机名，存储在hname指向的缓冲区中。参数：指向缓冲区的指针、缓冲区的大小
    hent = gethostbyname(hname);  // 通过主机名获得主机信息，存储在结构体hent中
    // 遍历'hent->h_addr_list'列表，找到最后一个IP地址，使用'inet_ntoa'将其转换为字符串形式的点分10进制IP地址
    for (int i = 0; hent->h_addr_list[i]; i++) {
        ipC = inet_ntoa(*(struct in_addr *)(hent->h_addr_list[i]));   // 将网络字节序（大端序）表示的IPv4地址的‘strcut in_addr'结构体，转化为点分十进制的字符串格式
    }
    std::string ip = std::string(ipC);  // char* -> string
    
    // 记录IP和端口到配置文件
    std::string node = "node" + std::to_string(nodeINdex); // 节点名称
    std::ofstream outfile;  // 输出流
    outfile.open("test.conf", std::ios::app);   // 打开或创建文件'test.conf'并以追加模式'std::ios:app'写入
    if (!outfile.is_open()){
        std::cout << "打开文件失败！" << std::endl;
        exit(EXIT_FAILURE);
    }
    outfile << node + "ip=" + ip << std::endl;
    outfile << node + "port=" + std::to_string(port) << std::endl;
    outfile.close();

    // 配置并启动Muduo库的TCP服务器
    muduo::net::InetAddress address(ip, port);  // 创建IP地址和端口的InetAddress对象
    // 创建TcpServer对象，使用共享指针指向该对象，与事件循环、IP地址等关联
    m_muduo_server = std::make_shared<muduo::net::TcpServer>(&m_eventLoop, address, "RcpProvider");
    // 设置TcpServer对象的回调函数
    m_muduo_server->setConnectionCallback(std::bind(&RpcProvider::onConnection, this, std::placeholders::_1));
    m_muduo_server->setMessageCallback(std::bind(&RpcProvider::onMessage, this, 
                                        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    m_muduo_server->setThreadNum(4);   // 设置TCP服务器的线程数目为4

    std::cout << "RpcProvider start service at ip: " << ip << " port: " << port << std::endl;

    m_muduo_server->start();  // 启动TCP服务器，开始监听和接受连接
    m_eventLoop.loop();  // 进入事件循环，开始处理事件（如连接、消息）
}
/*
netdb.h中的hostent结构体
struct hostent {
    char    *h_name;        // Official name of the host.
    char    **h_aliases;    // Alias list.
    int     h_addrtype;     // Host address type.
    int     h_length;       // Length of address.
    char    **h_addr_list;  // List of addresses from name server. 每个元素都指向一个网络字节序形式的IP地址，为char*
    #define h_addr  h_addr_list[0]  // Address, for backward compatibility.
};
*/


/*
onConnection 回调函数
目的：处理新连接或断开连接事件
步骤：
    1. 如果断开连接，则关闭连接
    2. 如果是新连接什么都不干，正常连接即可
*/
void RpcProvider::onConnection(const muduo::net::TcpConnectionPtr& conn) {
    // 断开连接
    if (!conn->connected()) conn->shutdown();
    // 新连接什么都不干
}


/*
onMessage 回调函数
目的：处理已建立连接用户的读写事件，即处理RPC请求
步骤：
    1. 解析请求数据，获取服务名、方法名和参数
    2. 查找对应的服务对象和方法对象
    3. 生成请求和响应对象
    4. 绑定一个'Closure'回调函数
    5. 调用服务对象的方法
*/
void RpcProvider::onMessage(const muduo::net::TcpConnectionPtr &conn, 
                            muduo::net::Buffer* buffer, muduo::Timestamp time) {
    std::string recv_buf = buffer->retrieveAllAsString();  // 从缓冲区中提取所有数据作为字符串
    // 使用protobuf的ArrayInputStream和CodedInputStream解析接收到的数据，用于处理序列化和反序列化的数据流
    google::protobuf::io::ArrayInputStream array_input(recv_buf.data(), recv_buf.size());  // 将原始字节数组封装为一个输入流，以便后续通过更高级的输入流类解析
    google::protobuf::io::CodedInputStream coded_input(&array_input);  // 根据编码规则，解析数据，提取出实际的字段值

    uint32_t header_size{}; // 标准无符号32位整形变量
    coded_input.ReadVarint32(&header_size);   // 读取头部大小（4字节，32位）

    // 读取并解析RPC请求头
    std::string rpc_header_str;
    RPC::RpcHeader rpcHeader;    // RPC请求头类，protobuf生成
    std::string service_name;
    std::string method_name;
    // 设置读取限制，保证只读取header_size大小的数据
    google::protobuf::io::CodedInputStream::Limit msg_limit = coded_input.PushLimit(header_size);
    coded_input.ReadString(&rpc_header_str, header_size);  // 将头部信息读取到'rpc_header_str'中
    coded_input.PopLimit(msg_limit);  // 取消加的限制
    // 解析请求头
    uint32_t args_size{};
    if (rpcHeader.ParseFromString(rpc_header_str)) {  // 反序列化头部信息
        service_name = rpcHeader.service_name();
        method_name = rpcHeader.method_name();
        args_size = rpcHeader.args_size();  // 请求参数大小
    }
    else {   // 反序列化失败
        std::cout << "rpc_header_str: " << rpc_header_str << " parse error!" << std::endl;
        return;
    }

    // 读取并解析RPC请求参数
    std::string args_str;
    bool read_args_success = coded_input.ReadString(&args_str, args_size);  // 请求存储到了args_str中
    if (!read_args_success) return;   // 读取失败

    // 查找服务对象
    auto it = m_serviceMap.find(service_name);   // 利用服务名称在哈希表中寻找服务对象信息
    if (it == m_serviceMap.end()) {  // 没找到
        std::cout << "服务：" << service_name << " is not exist!" << std::endl;
    }

    // 查找方法对象
    auto mit = it->second.m_methodMap.find(method_name);
    if (mit == it->second.m_methodMap.end()) {  // 没找到
        std::cout << service_name << ": " << method_name << " is not exist!";
    }

    // 获取对象和方法
    google::protobuf::Service* service = it->second.m_service;
    const google::protobuf::MethodDescriptor *method = mit->second;

    // 创建请求对象并解析参数
    google::protobuf::Message *request = service->GetRequestPrototype(method).New(); // 创建一个新的请求消息对象request，类型与method的请求消息类型匹配
    if (!request->ParseFromString(args_str)) {  // 解析请求参数，若解析失败
        std::cout << "request parse error, content: " << args_str << std::endl;
    }

    // 创建响应对象
    google::protobuf::Message *response = service->GetResponsePrototype(method).New();
    
    // 创建回调函数，用于在服务方法执行完毕后向客户端发送响应
    /*
    参数解析：
      this是指当前的 RpcProvider 实例。在回调时，SendRpcResponse 方法会在这个实例上调用。
      &RpcProvider::SendRpcResponse: 指向 RpcProvider 类的成员函数 SendRpcResponse 的指针。在这里是指这个回调函数绑定的函数
      coon：对应const::muduo::net::TcpConnectionPtr &   是SendRpcResponse的输入参数
      response：对应google::protobuf::Message *         是SendRpcResponse的输入参数
    */
    google::protobuf::Closure *done = google::protobuf::NewCallback<RpcProvider, const::muduo::net::TcpConnectionPtr &, google::protobuf::Message *>(this, &RpcProvider::SendRpcResponse, conn, response);  

    // 调用服务方法
    service->CallMethod(method, nullptr, request, response, done);
}


/*
SendRpcResponse 方法
目的：RPC服务方法调用完成后，序列化RPC响应，发送给客户端
步骤：
    1. 将响应对象序列化为字符串
    2. 通过连接发送响应数据
*/
void RpcProvider::SendRpcResponse(const muduo::net::TcpConnectionPtr &conn, google::protobuf::Message *response) {
    std::string response_str;
    if (response->SerializeToString(&response_str)){ // 序列化
        // 成功，则发送响应
        conn->send(response_str);
    }
    else {  // 失败
        std::cout << "serialize response_str error!" << std::endl;
    }
      
}


/*
析构函数
目的：清理资源，退出事件循环
步骤：
    1. 打印服务的IP和端口信息
    2. 退出事件循环
*/
RpcProvider::~RpcProvider() {
    std::cout << "[func = RpcProvider::!RpcProvider()]: ip和port信息: " << m_muduo_server->ipPort() << std::endl;
    m_eventLoop.quit();
}
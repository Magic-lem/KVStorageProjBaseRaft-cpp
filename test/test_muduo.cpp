// 测试muduo库代码
// 使用muduo库实现一个简单的echo回显服务器
// ～ by magic_pri 2024.6.15

#include <muduo/net/TcpServer.h>
#include <muduo/base/Logging.h>
#include <boost/bind/bind.hpp>
#include <muduo/net/EventLoop.h>

// 使用muduo开发回显服务器
class EchoServer{
public:
    // 构造函数，输入参数
    // loop：EentLoop类指针，用于事件循环
    // listenAddr: InetAddress类对象，服务端的地址结构
    EchoServer(muduo::net::EventLoop* loop, const muduo::net::InetAddress& listenAddr);    
    
    void start();

private:

    // 连接建立或关闭时的回调函数
    void onConnection(const muduo::net::TcpConnectionPtr& conn);

    // 收到消息时的回调函数
    void onMessage(const muduo::net::TcpConnectionPtr& conn, muduo::net::Buffer* buf, muduo::Timestamp time);

    muduo::net::TcpServer server_;  // 私有成员变量：TcpServer类对象，管理所有连接
};

// 构造函数实现：初始化TcpServer对象并设置回调函数
EchoServer::EchoServer(muduo::net::EventLoop* loop, 
                        const muduo::net::InetAddress& listenAddr)
                        : server_(loop, listenAddr, "EchoServer")   // 列表初始化成员变量
{   
    // 使用boost::bind绑定回调函数
    // [this]是捕获当前'EchoServer'对象的this指针，以便可以访问其成员函数
    // _1, _2, _3为占位符，表示回调函数应当传入的几个参数
    server_.setConnectionCallback(boost::bind(&EchoServer::onConnection, this, boost::placeholders::_1)); // 绑定建立或关闭连接时的回调函数
    server_.setMessageCallback(boost::bind(&EchoServer::onMessage, this, boost::placeholders::_1, boost::placeholders::_2, boost::placeholders::_3));   // 绑定获取消息的回调函数
}

// start的实现：启动服务器，开始监听端口并接受新的连接
void EchoServer::start(){
    server_.start();
}

// 回调函数的实现
void EchoServer::onConnection(const muduo::net::TcpConnectionPtr& conn){
    // LOG_INFO，muduo库中的打印日志方法
    LOG_INFO << "EchoServer - " << conn->peerAddress().toIpPort() << " -> "
             << conn->localAddress().toIpPort() << " is "
             << (conn->connected() ? "UP" : "DOWN");
}

void EchoServer::onMessage(const muduo::net::TcpConnectionPtr& conn, muduo::net::Buffer* buf, 
                            muduo::Timestamp time){
    // 接收到所有消息，然后回显
    muduo::string msg(buf->retrieveAllAsString());  // 将缓冲区消息提取成muduo::string

    // 输出日志，记录接收到的消息
    LOG_INFO << conn->name() << " echo " << msg.size() << " bytes, " 
             << "data received at " << time.toString();

    // 回显，发送给对端（服务端发送给客户端，客户端发送给服务端）
    conn->send(msg);
}


int main(){                            
    // 打印日志，输出当前进程的PID
    LOG_INFO << "pid = " << getpid();

    // 创建循环事件对象
    muduo::net::EventLoop loop;

    // 创建InetAdress对象，表示服务器监听的地址和端口
    muduo::net::InetAddress listenAdrr(8888);

    // 创建EchoServer对象
    EchoServer server(&loop, listenAdrr);

    // 启动服务器，开始监听
    server.start();

    // 进入事件循环
    loop.loop();
}
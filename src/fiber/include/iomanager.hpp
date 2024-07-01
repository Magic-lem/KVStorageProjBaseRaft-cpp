//
// 基于epoll的I/O管理器‘IOManager’的定义，提供对文件描述符的读写等事件的管理功能
// created by magic_pri on 2024-6-30
//
#ifndef __SYLAR_IOMANAGER_H__
#define __SYLAR_IOMANAGER_H__

#include "scheduler.hpp"
#include "timer.hpp"
#include <sys/epoll.h>
#include <string.h>
#include <fcntl.h>

namespace monsoon {

enum Event {       // 事件类型，包括无事件、读事件、写事件
    NONE = 0x0,
    READ = 0x1,
    WRITE = 0x4,
};

// 定义事件所需要具有的上下文信息
struct EventContext {   
    Scheduler *scheduler = nullptr;     // 调度器
    Fiber::ptr fiber;                   // 协程指针
    std::function<void()> cb;           // 回调函数
};  

/*
FdContext
主要作用：封装某个文件描述符的各事件以及事件上下文信息
*/
class FdContext {
   
friend class IOManager;

public:
    EventContext &getEveContext(Event event);  // 获取事件上下文信息
    void resetEveContext(EventContext &ctx);    // 重置事件上下文
    void triggerEvent(Event event);  // 触发事件

private:
    EventContext read;      // 读事件的上下文
    EventContext write;     // 写事件的上下文
    int fd = 0;             // 文件描述符
    Event events = NONE;    // 当前的事件类型，事件状态。因为只有三种类型，可以通过位操作来进行操作
    Mutex mutex;            // 互斥锁
};

/*
IOManager IO管理类
主要作用：实现基于epoll的I/O事件管理，添加、删除、取消文件符上的读写事件，并在事件就绪时触发相应的回调函数或协程
*/
class IOManager: public Scheduler, public TimerManager {
public:
    typedef std::shared_ptr<IOManager> ptr;

    // 构造与析构函数
    IOManager(size_t threads = 1, bool use_caller = true, const std::string &name = "IOManager"); 
    ~IOManager();

    // 事件管理函数
    int addEvent(int fd, Event event, std::function<void()> cb = nullptr);  // 添加事件
    bool delEvent(int fd, Event event);  // 删除事件
    bool cancelEvent(int fd, Event event);    // 取消事件
    bool cancelAll(int fd);  // 取消所有事件

    // 静态函数
    static IOManager *GetThis();    // 获取当前IOManager的实例

protected:
    // 调度器相关函数，继承自Scheduler和TimerManager

    void tickle() override;  // 通知调度器任务到达，需要调度
    bool stopping() override;   // 判断IOManager是否可以停止
    void idle() override;       // idle状态

    bool stopping(uint64_t &timeout);    // 判断IOManager是否可以停止，并获取最近一个定时器超时时间

    void OnTimerInsertedAtFront() override; // 定时器插入到了最前面的额外处理

    void contextResize(size_t size);  // 调整上下文大小

private:
    int epfd_ = 0;  // epoll文件描述符
    int tickleFds_[2];   // 用于唤醒epoll的管道
    std::atomic<size_t> pendingEventCnt_ = {0};     // 正在等待执行的I/O事件数量
    RWMutex mutex_;
    std::vector<FdContext *> fdContexts_;       // 存储文件描述符的上下文
};

}


#endif
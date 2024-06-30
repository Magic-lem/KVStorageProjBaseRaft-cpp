//
// I/O管理器的具体实现
// created by magic_pri on 2024-6-30
//

#include "./include/iomanager.hpp"

namespace monsoon {

/*
gFdContext::getEveContext
主要功能：获取事件的上下文
输入参数：Event类型的枚举值，指示事件类型
*/
EventContext &FdContext::getEveContext(Event event) {
    switch (event) {
        case READ:
            return read;
        case WRITE:
            return write;
        default:
            CondPanic(false, "getEveCOntext error: unknow event");  // 错误的事件类型
    }
    // 由于default分支没有返回值，这里抛出异常，声明事件类型无效。而不是返回一个无效的引用
    throw std::invalid_argument("getContext invalid event"); 
}

/*
gFdContext::resetEveContext
主要功能：重置事件上下文
输入参数：EventContext，事件上下文类型对象
*/
void FdContext::resetEveContext(EventContext &ctx) {
    ctx.scheduler = nullptr;
    ctx.fiber.reset();  // 共享指针reset重置
    ctx.cb = nullptr;
}

/*
gFdContext::triggerEvent
主要功能：触发事件，将事件所对应的fiber或cb加入到调度器的任务列表
输入参数：Event类型的枚举值，指示事件类型
*/
void FdContext::triggerEvent(Event event) {
    CondPanic(events & event, "event hasn't been registed");    // events与event按位与，检查event是否已经注册
    // 更新事件状态
    events = (Event) (events & ~event);  // 移除event，表示已触发

    EventContext &ctx = getEveContext(event);  // 获取事件上下文的引用，实际为FdContext对象中的事件上下文
    // 将任务添加到调度器的任务列表中
    if (ctx.cb) {
        ctx.scheduler->scheduler(ctx.cb);   
    } else {
        ctx.scheduler->scheduler(ctx.fiber);
    }
    // 重置事件的上下文
    resetEveContext(ctx);
    return;
}

/*
IOManager::IOManager 构造函数
主要作用：初始化IOManager对象，包括创建epoll实例、管道、注册管道读事件到epoll实例
*/
IOManager::IOManager(size_t threads, bool use_caller, const std::string &name) : Scheduler(threads, use_caller, name) { 
    epfd_ = epoll_create(5000);     // 创建一个epoll文件描述符实例，指定监听数目

    // 创建一个管道，将返回的文件描述符存储在tickleFds_中，tickleFds_[0]为读端，tickleFds_[1]为写端
    int ret = pipe(tickleFds_);     
    CondPanic(ret == 0, "pipe error");

    // 初始化epoll事件
    epoll_event event{};        // epoll_event结构体
    memset(&event, 0, sizeof(epoll_event)); // 清零，初始化
    event.events = EPOLLIN | EPOLLET;   // epoll关注读事件，使用边缘触发模式
    event.data.fd = tickleFds_[0];   // epoll的文件描述符 —— 管道读端

    // 设置管道为非阻塞模式
    ret = fcntl(tickleFds_[0], F_SETFL, O_NONBLOCK);
    CondPanic(ret == 0, "set for nonblock error");

    // 注册管道读描述符到epoll实例，并关联事件event
    ret = epoll_ctl(epfd_, EPOLL_CTL_ADD, tickleFds_[0], &event);
    CondPanic(ret == 0, "epoll_ctl error");

    // 调整上下文大小
    contextResize(32);

    // 启动IOManager调度器(继承自Scheduler)，开始协程的调度和管理
    start();
}

/*
IOManager::~IOManager 析构函数
主要作用：清理资源
*/
IOManager::~IOManager() {
    stop(); // 停止IOManager调度器
    close(epfd_);  // 关闭epoll文件描述符
    close(tickleFds_[0]);  // 关闭管道读描述符
    close(tickleFds_[1]);  // 关闭管道写描述符

    // 释放所有事件上下文对象
    for (size_t i = 0; i < fdContexts_.size(); ++i) {
        if (fdContexts_[i]){
            delete fdContexts_[i];  // 是动态开辟的指针，需要释放资源
        }
    }
}

/*
int IOManager::addEvent
主要作用：向epoll实例添加或修改文件描述符的事件，并将事件与回调函数或协程关联起来
输入参数：   int fd 文件描述符
            Event event 事件类型
            std::function<void()> cb 回调函数
主要步骤：
        1. 获取或创建文件描述符fd的上下文信息对象FdContext
        2. 检查在该描述符fd上是否已经注册过了相同的事件
        3. 构造epoll事件，注册到epoll实例中
        4. 更新fd所对应的FdContext对象以及EventContext对象的状态
        5. 返回成功或失败状态
*/
int IOManager::addEvent(int fd, Event event, std::function<void()> cb) {
    FdContext *fd_ctx = nullptr;
    RWMutex::ReadLock lock(mutex_);  // 加读锁

    // 找到fd所对应的上下文信息
    if ((int)fdContexts_.size() > fd) {
        fd_ctx = fdContexts_[fd];   // 是个指针
        lock.unlock();
    } else {
        // 没有，则创建上下文信息
        lock.unlock();
        RWMutex::WriteLock lock2(mutex_);
        contextResize(fd * 1.5);    // 因为fd超出了范围，所以应该首先扩大fdContexts_数组
        fd_ctx = fdContexts_[fd];   // 是个指针
    }

    // 检查是否存在重复事件，同一个fd不允许注册重复事件
    Mutex::Lock ctxLock(fd_ctx->mutex);     // 互斥锁
    CondPanic(!(fd_ctx->events & event), "addevent error, fd = " + fd);

    // 如果fd_ctx已存在事件，则是修改，否则是新增
    int op = fd_ctx->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    epoll_event epevent;  // 构造epoll事件
    epevent.events = EPOLLET | fd_ctx->events | event;    // 设置为边缘触发，并包含新的事件（event）和已有的事件（fd_ctx->events)
    epevent.data.ptr = fd_ctx;  // 上下文

    // 将事件注册到epoll实例
    int ret = epoll_ctl(epfd_, op, fd, &epevent);
    if (ret) {
        std::cout << "addevent: epoll_ctl error" << std::endl;
        return -1;
    }
    ++pendingEventCnt_;   // 增加待处理I/O事件的个数

    // 更新fd_ctx的事件状态
    fd_ctx->events = (Event)(fd_ctx->events | event);   // 将event加入到事件类型中
    EventContext &event_ctx = fd_ctx->getEveContext(event);  // 获取对应事件的上下文信息
    CondPanic(!event_ctx.scheduler && !event_ctx.fiber && !event_ctx.cb, "event_ctx is nullptr");   // 检查是否为空，为空报错

    // 将当前调度器设置为event_ctx的调度器
    event_ctx.scheduler = Scheduler::GetThis();

    if (cb) {   // 如果有回体哦阿函数，则将其交换到event_ctx中
        event_ctx.cb.swap(cb);
    } else {    // 没有提供回调函数，则将当前协程设置为event_ctx的协程
        event_ctx.fiber = Fiber::GetThis();
        CondPanic(event_ctx.fiber->getState() == Fiber::RUNNING, "state=" + event_ctx.fiber->getState());   // 需要是RUNNING状态的协程
    }

    std::cout << "add event success, fd = " << fd << std::endl;
    return 0;
}


/*
bool IOManager::delEvent 删除事件
主要功能：删除指定的事件，且删除前不会主动触发事件
输入参数：  
        int fd 文件描述符
        Event event 事件类型
*/
bool IOManager::delEvent(int fd, Event event) {
    RWMutex::ReadLock lock(mutex_); // 读锁，保护贡献资源fdContexts_

    // 找不到事件，直接返回
    if ((int)fdContexts_.size() <= fd) {
        return false;
    }

    FdContext *fd_ctx = fdContexts_[fd];    // fd对应的上下文对象
    lock.unlock();  

    Mutex::Lock ctxLock(fd_ctx->mutex);   // 互斥锁，修改fd_ctx内容
    if (!(fd_ctx->events & event)) {    // 文件描述符没有注册event类型的事件，直接返回
        return false;   
    }

    Event new_events = (Event)(fd_ctx->events & ~event);    // 删除指定事件，获得删除后的事件状态
    int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;  // 根据是否还剩下实现决定是执行修改操作还是删除操作，用于epoll系统调用
    epoll_event epevent;  // epoll事件
    epevent.events = EPOLLET | new_events;   // 边缘触发 + 删除后的事件情况
    epevent.data.ptr = fd_ctx;

    // 注册删除事件后的结果
    int ret = epoll_ctl(epfd_, op, fd, &epevent);
    if (ret) {
        std::cout << "delevent: epoll_ctl error" << std::endl;
        return false;
    }

    // 更新事件计数
    --pendingEventCnt_;

    // 清理上下文
    fd_ctx->events = new_events;
    EventContext &event_ctx = fd_ctx->getEveContext(event);     // 找到删除事件的上下文信息对象
    fd_ctx->resetEveContext(event_ctx);     // 重置该上下文信息
    return true;
}


}
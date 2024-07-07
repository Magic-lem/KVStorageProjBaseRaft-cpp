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

/*
bool IOManager::cancelEvent 取消IO事件，
主要功能：取消事件，取消前会主动触发事件
*/
bool IOManager::cancelEvent(int fd, Event event) {
  RWMutex::ReadLock lock(mutex_);
  if ((int) fdContexts_.size() < fd) {  // 没有这个文件描述符
    return false;
  }

  // 存在这个文件描述符，则找到它对应的事件上下文对象
  FdContext *fd_ctx = fdContexts_[fd];
  lock.unlock();

  Mutex::Lock ctxLock(fd_ctx->mutex);  // 给事件上下文对象加互斥锁，会修改
  if (!(fd_ctx->events & event)) {  // 不存在指定事件
    return false;
  }

  // 存在指定事件，则开始取消
  Event new_events = (Event)(fd_ctx->events & ~event); // 从events中删除event
  int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;   // 根据删除event之后是否还存在事件，决定是执行修改还是删除
  epoll_event epevent;
  epevent.events = EPOLLET | new_events;
  epevent.data.ptr = fd_ctx;

  // 注册删除后的事件作为fd文件描述符的监听事件
  int ret = epoll_ctl(epfd_, op, fd, &epevent);
  if (ret) {
    std::cout << "cancelevent: epoll_ctl error" << std::endl;
    return false;
  }

  // 删除上下文之前，触发此事件
  fd_ctx->triggerEvent(event);    // 触发后会自动删除
  --pendingEventCnt_;
  return true;

}

/*
bool IOManager::cancelAll 取消所有事件
主要功能：取消对文件描述符fd的所有事件监听，删除前触发所有事件
*/
bool IOManager::cancelAll(int fd) {
  RWMutex::ReadLock lock(mutex_);
  if ((int)fdContexts_.size() <= fd) {  // 没有该文件描述符
    return false;
  }
  FdContext *fd_ctx = fdContexts_[fd];
  lock.unlock();

  Mutex::Lock ctxLock(fd_ctx->mutex);
  if (!fd_ctx->events) {
    return false;   // 如果不存在事件，直接返回
  }

  // 创建epoll删除事件  —— 创建空epoll事件
  int op = EPOLL_CTL_DEL;
  epoll_event epevent;
  epevent.events = 0;   // 事件为空
  epevent.data.ptr = fd_ctx;

  // 注册删除事件 —— 将事件为空的epoll事件注册为fd的监听事件，表示取消对fd文件描述符的事件监听
  int ret = epoll_ctl(epfd_, op, fd, &epevent);
  if (ret) {
    std::cout << "cancelall: epoll_ctl error" << std::endl;
    return false;
  }

  // 触发所有fd文件描述符所注册事件（读和写）
  if (fd_ctx->events & READ) {  // 存在读事件
    fd_ctx->triggerEvent(READ);
    --pendingEventCnt_;
  }
  if (fd_ctx->events & WRITE) { // 存在写事件
    fd_ctx->triggerEvent(WRITE);
    --pendingEventCnt_;
  }

  // 确保fd所对应的事件全部被取消
  CondPanic(fd_ctx->events == 0, "fd not totally clear");
  return true;
}

/*
static IOManager *IOManager::GetThis  静态成员函数
主要作用：获取当前线程的IOManager的实例
*/
IOManager * IOManager::GetThis() {
  return dynamic_cast<IOManager *> (Scheduler::GetThis());
}

/*
void IOManager::tickle 
主要作用：通知调度器有任务到来
*/
void IOManager::tickle() {
  if (!isHasIdleThreads()) {
    return;  // 没有空闲的线程
  }

  // 存在空闲的线程，空闲线程应在执行idle协程（阻塞于epoll_wait），则通过管道来使得idle协程中的epoll_wait返回，唤醒idle协程，从而能够检查任务队列，调度任务
  int rt = write(tickleFds_[1], "T", 1);    // 调用write系统调用，向管道写端写入数据，导致管道在读端会出现可读事件，进而epoll_wait返回
  CondPanic(rt == 1, "write pipe error");
}

/*
void IOManager::idle  空闲协程
主要作用：调度器无任务可执行时会阻塞等待事件发生
          当有新事件触发，则退出idle状态，则执行回调函数
          当有新的调度任务，则退出idle状态，并执行对应任务
*/
void IOManager::idle() {
  const uint64_t MAX_EVENTS = 256;  // 一次最多检测256个就绪事件(epoll监听事件)
  epoll_event *events = new epoll_event[MAX_EVENTS]();    // 用来存储已发生的事件
  // 使用 shared_ptr 自动管理 events 数组的生命周期，确保在 idle 方法结束时释放内存，避免内存泄漏
  std::shared_ptr<epoll_event> shared_events(events, [](epoll_event *ptr) {delete[] ptr;});  // 当shared_events销毁时，同时释放ptr（events）的内存

  while (true) {  // 无限循环，直到调度器退出才会终止
    // 检查是否可以停止，并获取最近一个定时器超时时间
    uint64_t next_timeout = 0;
    if (stopping(next_timeout)) { 
      std::cout << "name = " <<  GetName() << "idle stopping exit" << std::endl;  // 调度器停止，退出循环
      break;
    }

    // 阻塞等待，等待事件发生 或者 定时器超时
    int ret = 0;
    do {
      static const int MAX_TIMEOUT = 5000;

      // 设置超时时间最大不超过5000ms
      if (next_timeout != ~0ull) {
        next_timeout = std::min((int)next_timeout, MAX_TIMEOUT);    
      } else {
        next_timeout = MAX_TIMEOUT;
      }

      // 阻塞等待事件就绪
      ret = epoll_wait(epfd_, events, MAX_EVENTS, (int)next_timeout); // 阻塞，直到事件发生或者定时器超时，才会返回

      if (ret < 0) {   // 返回值为-1表示发生了错误
        if (errno == EINTR) { // 全局变量errno为EINTR，说明系统调用被中断，并不是致命错误，继续等待
          continue;
        }
        // 否则报错退出阻塞等待
        std::cout << "epoll_wait [" << epfd_ << "] errno, err: " << errno << std::endl;
        break;  // 退出等待循环
      } else {  // =0时，表示等待超时没有发生事件，此时退出等待； >0时表示成功返回，返回值为触发的事件数，同样退出等待
        break;
      }
    } while (true);

    // 收集所有超时的定时器，执行这些定时器的回调函数
    std::vector<std::function<void()>> cbs;
    listExpiredCb(cbs);
    if (!cbs.empty()) {
      for (const auto &cb: cbs) {
        scheduler(cb);   // 添加到任务队列中
      }
      cbs.clear();    // 清空
    }

    // 处理事件（如果存在事件被触发）
    for (int i = 0; i < ret; i++) {
      epoll_event &event = events[i]; // events存储的是被触发的事件
      if (event.data.fd == tickleFds_[0]) { // 事件是管道读事件，说明有新的任务
        uint8_t dummy[256];
        while (read(tickleFds_[0], dummy, sizeof(dummy)) > 0);  // 阻塞读取管道数据，直到读取完成。（边缘触发模式，循环读取直到 EAGAIN 错误）
        continue;
      }

      // 否则，则不是管道读事件，不是新增任务
      // 首先获得FdContext
      FdContext *fd_ctx = (FdContext *)event.data.ptr;
      Mutex::Lock lock(fd_ctx->mutex);

      // 检查是否是错误事件、或挂起事件（对端关闭）
      if (event.events & (EPOLLERR | EPOLLHUP)) {
        std::cout << "error events" << std::endl;
        event.events |= (EPOLLIN | EPOLLOUT) & fd_ctx->events;   // 如果是错误或挂起事件，则重新将该文件上下文中注册的读写事件加入到当前事件，后续会直接触发
      }

      // 获得该文件描述符上下文中实际要发生的事件
      int real_events = NONE;
      if (event.events & EPOLLIN) {
        real_events |= READ;
      }
      if (event.events & EPOLLOUT) {
        real_events |= WRITE;
      }
      // 检查实际发生的事件是否被fd_ctx注册
      if ((fd_ctx->events & real_events) == NONE) { // 没有被注册的
        continue;   // 不用处理，直接下一个事件
      }

      // 实际发生的事件存在被注册的，则触发该事件，同时将剩下的事件重新加入到epoll_wait
      int left_events = (fd_ctx->events & !real_events);  // 剩下的事件
      int op = left_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
      event.events = EPOLLET | left_events;

      int ret2 = epoll_ctl(epfd_, op, fd_ctx->fd, &event);  // 将剩下的事件重新加入到epoll_wait，确保边缘触发模式下事件不会丢失。
      if (ret2) {
        std::cout << "epoll wait [" << epfd_ << "] errno, err: " << errno << std::endl;
        continue;
      }

      // 触发事件，将事件对应的回调函数或协程其加入到任务列表
      // TODO: 这里是否并没有充分检查real_events中的事件都被注册？  因为上面只能保证fd_ctx->events和real_events有交集
      if (real_events & READ) {
        fd_ctx->triggerEvent(READ);
        --pendingEventCnt_;
      }
      if (real_events & WRITE) {
        fd_ctx->triggerEvent(WRITE);
        --pendingEventCnt_;
      }
    }

    // 事件处理完毕，idle协程yield让出执行权，使得调度协程可以去调度新加入的任务
    Fiber::ptr cur = Fiber::GetThis();
    auto raw_ptr = cur.get();
    cur.reset();    // 重置共享指针
    raw_ptr->yield();   // 让出执行权
  }
}

/*
bool IOManager::stopping() 
主要作用：判断IOManager（作为调度器）是否可以停止
*/
bool IOManager::stopping() {
  uint64_t timeout = 0;
  return stopping(timeout);
}

/*
bool IOManager::stopping(uint64_t &)
主要作用：判断IOManager（作为调度器）是否可以停止，并获取最近一个定时器超时时间
*/
bool IOManager::stopping(uint64_t &timeout) {
  timeout = getNextTimer();
  // 所有待调度的I/O事件执行结束后，才允许退出
  return timeout == ~0ull && pendingEventCnt_ == 0 && Scheduler::stopping();
}

/*
void IOManager::contextResize
主要作用：调整存储上下文信息封装对象的数组fdContexts_的大小，为新扩大的位置初始化一个上下文信息封装对象
*/
void IOManager::contextResize(size_t size) {
  fdContexts_.resize(size);
  for (size_t i = 0; i < fdContexts_.size(); ++i) {
    if (!fdContexts_[i]) {  // 若不存在对象，则初始化一个
      fdContexts_[i] = new FdContext;
      fdContexts_[i]->fd = i;  // 数组的索引是用的文件描述符，所以这里可以直接将描述符设为i
    }
  }
}

/*
void IOManager::OnTimerInsertedAtFront()
主要作用：定时器插入到了最前面的额外处理，即紧急定时器，则需要唤醒空闲线程，使其及时处理这个新的定时器任务
*/
void IOManager::OnTimerInsertedAtFront() {
  tickle();  // 可能会比普通定时器到时间时的唤醒更加迅速和紧急，因为它需要尽快处理插入最前面的定时器
}


}
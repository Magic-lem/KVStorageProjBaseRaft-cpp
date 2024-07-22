//
// hook的具体实现
// created by magic_pri on 2024-6-27
//

#include "hook.hpp"
#include <dlfcn.h>  // dlsym, RTLD_NEXT
#include <cstdarg>
#include <string>
#include "fd_manager.hpp"
#include "fiber.hpp"
#include "iomanager.hpp"

namespace monsoon {
// 全局变量，记录当前线程是否启用hook
static thread_local bool t_hook_enable = false;
static int g_tcp_connect_timeout = 5000;    // TCP连接超市事件，单位毫秒

// 定义一个宏HOOK——FUN，接收参数XX
// 在宏展开始，XX将被替换为后续每个函数名，可以简化代码中需要对这些函数名进行统一操作的情况
#define HOOK_FUN(XX) \
  XX(sleep)          \
  XX(usleep)         \
  XX(nanosleep)      \
  XX(socket)         \
  XX(connect)        \
  XX(accept)         \
  XX(read)           \
  XX(readv)          \
  XX(recv)           \
  XX(recvfrom)       \
  XX(recvmsg)        \
  XX(write)          \
  XX(writev)         \
  XX(send)           \
  XX(sendto)         \
  XX(sendmsg)        \
  XX(close)          \
  XX(fcntl)          \
  XX(ioctl)          \
  XX(getsockopt)     \
  XX(setsockopt)

/*
hook_init：初始化钩子
主要功能：初始化所有函数指针，使其真正指向实际函数
虽然在hook.hpp中声明了这些函数指针，但这些指针并没有实际指向具体函数，因此这里要根据函数名称来在动态链接库中寻找这些具体函数，使指针指向它们
*/
void hook_init() {  // 初始化函数指针
  static bool is_inited = false;
  if (is_inited) {
    return;
  }
  // dlsym:Dynamic LinKinf Library.返回指定符号的地址
  // dlsym(RTLD_NEXT, #name)是一个函数，在下一个动态链接库中查找具有给定名称'name'的函数符号地址（由名称去在动态库中找地址）
  // RTLD_NEXT是一个特殊常量，告诉dlsym从调用它的动态链接库的下一个动态连接库查找
  // #name 在编译阶段将name转换为一个字符串字面量
  // name##_fun 函数指针类型转换，将获取到的地址转换为相应函数指针类型
  // 总之，这段代码是根据名字name，在动态链接库中查找其地址，将其转换为对应的函数指针
  #define XX(name) name##_f = (name##_fun)dlsym(RTLD_NEXT, #name);  // 定义宏：XX(name)，会被展开为(name##_fun)dlsym(RTLD_NEXT, #name)
    HOOK_FUN(XX); // 由于宏定义，在编译器HOOK_FUN(XX)会被替换为XX(leep)......所有，这样写可以简洁
  #undef XX

  // 初始化之后，可以直接利用全局变量name_f即函数指针，来调用具体函数
}

/*
通过将hook_init()函数放在静态变量中，可以使其main函数执行前初始化
*/
static uint64_t s_connect_timeout = -1;
struct _HOOKIniter
{
    _HOOKIniter() {
        hook_init();  // 放到构造函数里
        s_connect_timeout = g_tcp_connect_timeout;
    }
};
static _HOOKIniter hook_initer;  // 定义为静态变量，在main开始之前调用构造函数

// 判断和设置当前线程是否使用hook
bool is_hook_enable() { return t_hook_enable; }
void set_hook_enable(const bool flag) { t_hook_enable = flag; }


struct timer_info   // 结构体，保存定时器的状态信息
{
  int cancelled = 0;  // 是否被取消
};



/*
do_io：模板函数
主要作用：封装各种I/O操作，实现超时处理、非阻塞I/O等
Input: int fd ，文件描述符
       OriginFun fun，原始I/O函数
       const char *hook_fun_name，钩子函数名称
       uint32_t event，事件类型
       int timeout_so, 超时选项
       Args &&...agrs，折叠表达式（变参模板）
*/
template <typename OriginFun, typename... Args>
static ssize_t do_io(int fd, OriginFun fun, const char *hook_fun_name, uint32_t event, int timeout_so, Args &&...args) {
  if (!t_hook_enable) {   // 不使用钩子函数
    return fun(fd, std::forward<Args>(args)...);
  }
  // 为当前文件描述符创建上下文ctx
  FdCtx::ptr ctx = FdMgr::GetInstance()->get(fd);   // FdMgr::GetInstance() 单例实例，获取文件描述符上下文信息
  if (!ctx) {
    return fun(fd, std::forward<Args>(args)...);  // 没找到上下文，不使用钩子函数，直接用原始函数执行
  }
  // 文件已经关闭，设置错误码'EBADF'
  if (ctx->isClose()) {
    errno = EBADF;    // 设置全局变量的值为EBADF，表示无效的文件描述符
    return -1;
  }
  // 钩子函数是针对套接字的
  if (!ctx->isSocket() || ctx->getUserNonblock()) {   // 不是socket或者用户非阻塞模式，直接调用原函数。
    // TODO:疑问？ 为什么用户非阻塞要直接返回，那如何处理结果呢？
    return fun(fd, std::forward<Args>(args)...);
  }
  // 获取对应type的fd超时时间
  uint64_t to = ctx->getTimeout(timeout_so);
  std::shared_ptr<timer_info> tinfo(new timer_info);

// 非阻塞情况下的重试逻辑
retry:
  ssize_t n = fun(fd, std::forward<Args>(args)...);   // 调用原始I/O函数

  // 错误情况：若读取操作被信号中断，继续尝试直到成功
  while (n == -1 && errno == EINTR) {   // 表示读取操作被信号中断
    n = fun(fd, std::forward<Args>(args)...);
  }
  
  // 错误情况：若数据未就绪，则通过注册事件监听的方式让出执行权，等事件触发后返回执行。或直到超时也未完成，则返回错误信息
  if (n == -1 && errno == EAGAIN) {   // 表示数据未就绪
    IOManager *iom = IOManager::GetThis();
    Timer::ptr timer;
    std::weak_ptr<timer_info> winfo(tinfo);

    if (to != (uint64_t)-1) { // 若设置了超时时间
      timer = iom->addConditionTimer(   //   加入一个定时器，I/O 操作（如读取或写入）没有完成，定时器会触发回调函数，取消该 I/O 操作并设置相应的错误状态
          to,
          [winfo, fd, iom, event]() {      
            auto t = winfo.lock();        // 获取timer_info
            if (!t || t->cancelled) {       // 没有条件对象（定时器状况对象）或者定时器已经被取消了，则直接返回
              return;
            }
            t->cancelled = ETIMEDOUT;      // 设置tinfo->cancelled，表明定时器超时，已删除IO操作
            iom->cancelEvent(fd, (Event)(event));   // 取消I/O事件
          },
          winfo);
    }

    // 注册事件监听
    int rt = iom->addEvent(fd, (Event)(event));
    if (rt) {  // 事件注册失败
      std::cout << hook_fun_name << " addEvent(" << fd << ", " << event << ")";
      if (timer) {
        timer->cancel();
      }
      return -1;
    } else {  
      Fiber::GetThis()->yield();  // 事件注册成功，当前协程让出执行权（yield），等待事件触发或定时器超时
      // 当前协程被唤醒
      if (timer) {        // 如果还存在定时器，取消，防止不必要的回调
        timer->cancel();
      }
      if (tinfo->cancelled) {     // 如果tinfo->cancelled被设置，说明协程是由于超时被唤醒，I/O操作已经被取消，设置 errno 为相应的错误码并返回
        errno = tinfo->cancelled;
        return -1;
      }
      // 事件触发了，跳转到 retry 标签重新尝试 I/O 操作
      goto retry;     
    }
  }

  return n;
}


/**
以下函数，都是在进行IO操作时可能会用到的系统调用的hook实现
在hook中，通过协程和定时器实现非阻塞的IO操作和超时控制。这种方式使得程序可以更加高效地处理大量IO密集型任务，同时兼顾了对系统调用的控制和管理。
*/

extern "C" {
#define XX(name) name##_fun name##_f = nullptr;   // 预先声明和初始化一组函数指针变量，为后续通过 dlsym 的函数指针赋值操作做准备
HOOK_FUN(XX);
#undef XX

/*
sleep函数
主要功能：暂停当前线程的执行，直到指定的秒数过去或被信号打断。
在挂起期间，操作系统会将 CPU 分配给其他线程/协程运行。（这里实现的钩子函数则不是挂起线程，而是让协程让出执行权，实现在协程层面的调度）
当 usleep 时间结束后，线程会被唤醒并继续执行。
*/
unsigned int sleep(unsigned int seconds) {
  // 如果设置了不使用钩子，则直接使用系统调用函数
  if (!t_hook_enable) {
    return sleep_f(seconds);
  }

  // 使用hook，则通过定时器实现：先让当前协程让出执行权，通过定时器在seconds秒之后重启
  Fiber::ptr fiber = Fiber::GetThis();
  IOManager *iom = IOManager::GetThis();
  // 添加一个定时器，回调函数为scheduler（添加任务到调度器中）。当定时器到时时，会将本协程加入到任务列表中
  iom->addTimer(seconds * 1000,
                std::bind((void (Scheduler::*)(Fiber::ptr, int thread)) & IOManager::scheduler, iom, fiber, -1));
  Fiber::GetThis()->yield();   // 让出执行权
  return 0;  // 睡眠完毕
}

/*
usleep函数
主要功能：暂定线程运行指定的微秒数
*/
int usleep(useconds_t usec) {
  if (!t_hook_enable) {
    auto ret = usleep_f(usec);
    return 0;
  }

  // 定时器
  Fiber::ptr fiber = Fiber::GetThis();
  IOManager *iom = IOManager::GetThis();
  iom->addTimer(usec / 1000,
                std::bind((void (Scheduler::*)(Fiber::ptr, int thread)) & IOManager::scheduler, iom, fiber, -1));
  Fiber::GetThis()->yield();
  return 0;
};

/*
nanosleep 函数
主要功能：暂停当前线程执行指定的纳秒数
*/
int nanosleep(const struct timespec *req, struct timespec *rem) {
  if (!t_hook_enable) {
    // 不允许hook,则直接使用系统调用
    return nanosleep_f(req, rem);
  }
  // 允许hook,则直接让当前协程退出，seconds秒后再重启（by定时器）
  Fiber::ptr fiber = Fiber::GetThis();
  IOManager *iom = IOManager::GetThis();
  int timeout_s = req->tv_sec * 1000 + req->tv_nsec / 1000 / 1000;
  iom->addTimer(timeout_s,
                std::bind((void(Scheduler::*)(Fiber::ptr, int thread)) & IOManager::scheduler, iom, fiber, -1));
  Fiber::GetThis()->yield();
  return 0;
}

/*
socket 函数
主要作用：创建套接字，对Linux socket函数的钩子封装
输入参数：domain，协议域；type，套接字类型；protocol，协议。
*/
int socket(int domain, int type, int protocol) {
  if (!t_hook_enable) {
    return socket_f(domain, type, protocol);  // 直接通过系统调用返回
  }
  // 否则，创建套接字后将返回的描述符加入到文件描述符管理类
  int fd = socket_f(domain, type, protocol);
  
  if (fd == -1) {   // 套接字创建失败
    return fd;
  }

  // 获取文件描述符fd的上下文信息，如果没有则创建
  FdMgr::GetInstance()->get(fd, true);  
  return fd;
}

/*
connet_with_timeout 带有超时处理的连接函数，连接到指定的套接字地址（对Linux网络编程中的connect函数钩子封装）
主要作用：在非阻塞模式下调用系统的 connect，并使用定时器和事件驱动机制来处理超时和事件通知
参数：
    fd：文件描述符，指向需要连接的套接字。
    addr：指向 sockaddr 结构，包含目标地址。
    addrlen：地址的长度。
    timeout_ms：超时时间，单位为毫秒。
注意：本函数逻辑更复杂，所以没有使用do_io函数模板
*/
int connect_with_timeout(int fd, const struct sockaddr *addr, socklen_t addrlen, uint64_t timeout_ms) {
  if (!t_hook_enable) {
    return connect_f(fd, addr, addrlen);
  }

  // 使用hook，则设置非阻塞套接字并调用系统的 connect 函数进行连接
  FdCtx::ptr ctx = FdMgr::GetInstance()->get(fd); // 获取文件描述符的上下文信息，对于socket来说，该描述符被设为非阻塞模式
  if (!ctx || ctx->isClose()) {   // 描述符无效或文件已关闭
    errno = EBADF;
    return -1;
  }

  if (!ctx->isSocket()) { // 不是套接字，直接系统调用
    // 一般来说对非套接字文件描述符调用 connect，会返回一个错误并设置 errno 为 ENOTSOCK（表示该文件描述符不是一个套接字）
    return connect_f(fd, addr, addrlen);  
  }

  // 检查fd是否被显式地设置为了非阻塞模式，如果是可以直接系统调用(已经在用户层面实现非阻塞了，这里不需要了)
  if (ctx->getUserNonblock()) {
    return connect_f(fd, addr, addrlen);
  }

  int n = connect_f(fd, addr, addrlen);   // fd被设为非阻塞模式，此时将以非阻塞模式执行
  if (n == 0) { // 立即连接成功
    return 0;
  } else if (n != -1 || errno != EINPROGRESS) { // 连接失败
    return n;  // 返回失败码
  }

  // 其他情况：errno == EINPROGRESS  表示还在进行连接，尚未完成。此时，注册WRITE事件来监听连接是否成功，同时设置一个定时器
  IOManager *iom = IOManager::GetThis();
  Timer::ptr timer;
  std::shared_ptr<timer_info> tinfo(new timer_info);
  std::weak_ptr<timer_info> winfo(tinfo);

  // 保证超时参数有效(不是-1)，添加条件定时器
  if (timeout_ms != (uint64_t)-1) {
    timer = iom->addConditionTimer(timeout_ms, 
                                   [winfo, fd, iom]() {
                                    auto t = winfo.lock();
                                    if (!t || t->cancelled) {   // 没有条件对象或者定时器已经被取消了
                                      return;
                                    }
                                    // 超时了，取消WRITE事件
                                    t->cancelled = ETIMEDOUT;
                                    iom->cancelEvent(fd, WRITE);
                                   },
                                   winfo);
  }

  
  // 对描述符fd添加WRITE事件（套接字变为可写状态表明连接已经成功建立）
  int rt = iom->addEvent(fd, WRITE);
  if (rt == 0) {
    Fiber::GetThis()->yield();   // 让出协程执行权，等待连接完成或超时
    // 协程拿到执行权，此时要么连接完成，要么超时了
    if (timer) {
      timer->cancel();  // 先把定时器取消
    }
    if (tinfo->cancelled) {   // 超时，返回错误信息
      errno = tinfo->cancelled;
      return -1;
    }
  } else {  // 添加事件失败
    if (timer) {
      timer->cancel();
    }
    std::cout << "connect addEvent(" << fd <<" , WORITE) error" << std::endl;
  }
  
  // 检查连接状态(套接字可写，不能保证连接一定成功建立，需要排查)
  int error = 0;
  socklen_t len = sizeof(int);

  // 首先getsockopt函数获取套接字错误状态
  if (-1 == getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len)) {
    // 返回-1，说明获取套接字错误状态失败
    return -1;
  }

  // 获取成功后，error中保存了套接字的错误状态，检查
  if (error) {
    // 存在错误
    errno = error;
    return -1;
  } else {
    // 没有错误，连接成功
    return 0;
  }
}

/*
connect 连接函数
主要作用：与connect_with_timeout类似，但是是用默认的超时时间
输入参数：
        sockfd，套接字描述符；
        addr，连接目标地址；
        addrlen，地址长度。
*/
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
  return monsoon::connect_with_timeout(sockfd, addr, addrlen, s_connect_timeout);
}

/*
accept 接收函数，对Linux accept函数的钩子封装
主要作用：用于接受来自监听套接字的连接请求，返回一个新的套接字描述符，用于与客户端进行通信。原有的套接字继续监听
输入参数
    int s：监听套接字描述符。这个套接字已经绑定到一个特定的地址并且正在监听连接请求。
    struct sockaddr addr：指向一个sockaddr结构体的指针，在返回时该结构体将被填充为已连接客户端的地址信息。
    socklen_t addrlen：指向一个socklen_t变量的指针，该变量最初包含addr的长度，并且在返回时被设置为客户端地址的实际长度。
*/
int accept(int s, struct sockaddr *addr, socklen_t *addrlen) {
  int fd = do_io(s, accept_f, "accept", READ, SO_RCVTIMEO, addr, addrlen);  // fd为新的套接字描述符
  if (fd > 0) {
    FdMgr::GetInstance()->get(fd, true);  // 创建获得fd的上下文信息
  }
  return fd;
}

/*
read 函数
功能: 从文件描述符 fd 读取最多 count 个字节到缓冲区 buf 中。
输入参数:
    int fd: 文件描述符。
    void *buf: 目标缓冲区。
    size_t count: 要读取的字节数
*/
ssize_t read(int fd, void *buf, size_t count) { 
  return do_io(fd, read_f, "read", READ, SO_RCVTIMEO, buf, count); 
}

/*
readv 函数
功能: 从文件描述符 fd 读取数据到多个缓冲区 iov 中，iovcnt 是缓冲区数组的长度。
输入参数:
    int fd: 文件描述符。
    const struct iovec *iov: 缓冲区数组。
    int iovcnt: 缓冲区数组的长度。
*/
ssize_t readv(int fd, const struct iovec *iov, int iovcnt) {
  return do_io(fd, readv_f, "readv", READ, SO_RCVTIMEO, iov, iovcnt);
}

/*
recv 函数
功能: 从套接字 sockfd 接收数据到缓冲区 buf，最多接收 len 个字节，flags 为接收选项。
输入参数:
    int sockfd: 套接字文件描述符。
    void *buf: 缓冲区。
    size_t len: 缓冲区长度。
    int flags: 接收选项。
*/
ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
  return do_io(sockfd, recv_f, "recv", READ, SO_RCVTIMEO, buf, len, flags);
}

/*
recvfrom 函数
功能: 从套接字 sockfd 接收数据到缓冲区 buf，同时获取数据来源地址 src_addr。
输入参数:
    int sockfd: 套接字文件描述符。
    void *buf: 缓冲区。
    size_t len: 缓冲区长度。
    int flags: 接收选项。
    struct sockaddr *src_addr: 源地址结构体。
    socklen_t *addrlen: 源地址结构体长度。
*/
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen) {
  return do_io(sockfd, recvfrom_f, "recvfrom", READ, SO_RCVTIMEO, buf, len, flags, src_addr, addrlen);
}

/*
recvmsg 函数
功能: 从套接字 sockfd 接收消息到消息头结构体 msg 中。
输入参数:
    int sockfd: 套接字文件描述符。
    struct msghdr *msg: 消息头结构体。
    int flags: 接收选项。
*/
ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags) {
  return do_io(sockfd, recvmsg_f, "recvmsg", READ, SO_RCVTIMEO, msg, flags);
}

/*
write 函数
功能: 将缓冲区 buf 中最多 count 个字节写入文件描述符 fd。
输入参数:
    int fd: 文件描述符。
    const void *buf: 缓冲区。
    size_t count: 要写入的字节数。
*/
ssize_t write(int fd, const void *buf, size_t count) {
  return do_io(fd, write_f, "write", WRITE, SO_SNDTIMEO, buf, count);
}

/*
writev 函数
功能: 将多个缓冲区 iov 中的数据写入文件描述符 fd，iovcnt 是缓冲区数组的长度。
输入参数:
    int fd: 文件描述符。
    const struct iovec *iov: 缓冲区数组。
    int iovcnt: 缓冲区数组的长度。
*/
ssize_t writev(int fd, const struct iovec *iov, int iovcnt) {
  return do_io(fd, writev_f, "writev", WRITE, SO_SNDTIMEO, iov, iovcnt);
}

/*
send 函数
功能: 将缓冲区 msg 中最多 len 个字节的数据发送到套接字 s，flags 为发送选项。
输入参数:
    int s: 套接字文件描述符。
    const void *msg: 缓冲区。
    size_t len: 缓冲区长度。
    int flags: 发送选项。
*/
ssize_t send(int s, const void *msg, size_t len, int flags) {
  return do_io(s, send_f, "send", WRITE, SO_SNDTIMEO, msg, len, flags);
}

/*
sendto 函数
功能: 将缓冲区 msg 中的数据发送到指定的套接字地址 to。
输入参数:
    int s: 套接字文件描述符。
    const void *msg: 缓冲区。
    size_t len: 缓冲区长度。
    int flags: 发送选项。
    const struct sockaddr *to: 目标地址。
    socklen_t tolen: 目标地址长度。
*/
ssize_t sendto(int s, const void *msg, size_t len, int flags, const struct sockaddr *to, socklen_t tolen) {
  return do_io(s, sendto_f, "sendto", WRITE, SO_SNDTIMEO, msg, len, flags, to, tolen);
}

/*
sendmsg 函数
功能: 将消息头结构体 msg 中的数据发送到套接字 s。
输入参数:
    int s: 套接字文件描述符。
    const struct msghdr *msg: 消息头结构体。
    int flags: 发送选项。
*/
ssize_t sendmsg(int s, const struct msghdr *msg, int flags) {
  return do_io(s, sendmsg_f, "sendmsg", WRITE, SO_SNDTIMEO, msg, flags);
}

/*
close 函数
功能: 关闭文件描述符 fd。
输入参数:
    int fd: 文件描述符。
*/
int close(int fd) {
  if (!t_hook_enable) {
    return close_f(fd);
  }

  // 清理与该文件描述符有关的上下文信息和事件
  FdCtx::ptr ctx = FdMgr::GetInstance()->get(fd);   // 获得文件描述符上下文
  if (ctx) {
    auto iom = IOManager::GetThis();
    if (iom) {
      iom->cancelAll(fd);   // 删除该文件描述符上的所有事件
    }
    FdMgr::GetInstance()->del(fd);  // 删除该文件描述符上下文
  }

  return close_f(fd); 
}

/*
fcntl： 对fcntl系统调用的封装，控制文件描述符属性
功能：控制文件描述符属性，如设置文件描述符为非阻塞模式、获取或设置文件描述符标志、复制文件描述符
输入参数:
    fd: 文件描述符。
    cmd: 控制命令，指定要执行的操作。
    ...: 可变参数，根据 cmd 的不同传入不同的参数。
*/
int fcntl(int fd, int cmd, .../* arg */) {
  // 由于使用了变参，需要处理
  va_list va;
  va_start(va,cmd);

  // 根据cmd不同进行不同的处理
  switch (cmd) {
    case F_SETFL:   // 设置文件描述符标志
    { 
      int arg = va_arg(va, int);  // 获取int类型变长参数的值（依次获取，会获取最前面的），即要设置的标志值
      va_end(va); // 将va重置为NULL
      
      FdCtx::ptr ctx = FdMgr::GetInstance()->get(fd);
      // 检查文件描述符上下文的有效性
      // TODO：疑问？ 这里的用户非阻塞模式和系统非阻塞模式的区别？   
      if (!ctx || ctx->isClose() || !ctx->isSocket()) { // 如果上下文无效、文件描述符已关闭、不是套接字，则直接调用系统调用
        // 对于不是套接字的文件描述符，不应该进行非阻塞标志的处理或其他特定于套接字的操作，而是应该尽快地交给操作系统处理
        return fcntl_f(fd, cmd, arg);
      }
      ctx->setUserNonblock(arg & O_NONBLOCK);   // 如果参数arg中包含O_NONBLOCK标志，则设置用户非阻塞标志

      // 调整标志值,确保了在调用 fcntl 时，arg 中的 O_NONBLOCK 状态与实际系统设置的非阻塞状态一致。
      if (ctx->getSysNonblock()) {  // 如果系统设置了非阻塞，则在arg中添加O_NONBLOCK标志
        arg |= O_NONBLOCK;
      } else {    // 否则删除O_NONBLOCK标志
        arg &= ~O_NONBLOCK;
      }
      return fcntl_f(fd, cmd, arg);
    } 
    break;
    case F_GETFL: // 获取文件描述符标志
    {
      va_end(va);
      int arg = fcntl_f(fd, cmd);  // 获取标志值

      FdCtx::ptr ctx = FdMgr::GetInstance()->get(fd); // 获取上下文
      // 检查上下文
      if (!ctx || ctx->isClose() || !ctx->isSocket()) {
        return arg;
      }

      // 是否阻塞取决于用户层面非阻塞标志
      if (ctx->getUserNonblock()) {
        return arg | O_NONBLOCK;
      } else {
        return arg & ~O_NONBLOCK;
      }
    }
    break;
    // 以下尚未实现
    case F_DUPFD:
    case F_DUPFD_CLOEXEC:
    case F_SETFD:
    case F_SETOWN:
    case F_SETSIG:
    case F_SETLEASE:
    case F_NOTIFY:
    #ifdef F_SETPIPE_SZ   // 如果定义了这个宏
      case F_SETPIPE_SZ:  // 应用于管道文件描述符，修改管道的buffer大小
    #endif
    {
      int arg = va_arg(va, int);
      va_end(va);
      return fcntl_f(fd, cmd, arg);
    }
    break;
    case F_GETFD:
    case F_GETOWN:
    case F_GETSIG:
    case F_GETLEASE:
    #ifdef F_GETPIPE_SZ
      case F_GETPIPE_SZ:  // 应用于管道文件描述符，获得管道的buffer大小
    #endif
    {
      va_end(va);
      return fcntl_f(fd, cmd);
    }
    break;
    case F_SETLK:
    case F_SETLKW:
    case F_GETLK: // 判断文件（记录）是否可以上struct flock*类型的锁
    {
      struct flock *arg = va_arg(va, struct flock *);
      va_end(va);
      return fcntl_f(fd, cmd, arg);
    }
    break;
    case F_GETOWN_EX:
    case F_SETOWN_EX: // 设定文件描述符属主，当文件描述符上可执行IO操作时会通知指定的进程或进程组 （一般用于异步I/O）
    {
        struct f_owner_exlock *arg = va_arg(va, struct f_owner_exlock *);
        va_end(va);
        return fcntl_f(fd, cmd, arg);
    }
    break;
    default:
      va_end(va);
      return fcntl_f(fd, cmd);
  }
}

/*
ioctl  对系统函数对 ioctl 函数的一个封装
主要作用： 处理非阻塞模式的设置，同时保留了用户层面的非阻塞状态
输入参数：
    int d：文件描述符（通常是一个套接字）。
    unsigned long int request：指定请求的操作码。
    ...：变长参数，具体取决于请求操作码。
*/
int ioctl(int d, unsigned long int request, ...) {
  va_list va;
  va_start(va, request);
  void *arg = va_arg(va, void *);
  va_end(va);

  if (request == FIONBIO) {   // 设置非阻塞模式
    bool user_nonblock = !!(int *)arg;  // arg先转换为整形指针，再由双重逻辑非运算!!转换为布尔值
    // 获取文件描述符上下文信息
    FdCtx::ptr ctx = FdMgr::GetInstance()->get(d);
    if (!ctx || ctx->isClose() || !ctx->isSocket()) {
      return ioctl_f(d, request, arg);
    }
    ctx->setUserNonblock(user_nonblock);
  }
}

/*
getsockopt 函数
主要作用：获取指定套接字选项的当前值，并将其存储在 optval 指向的缓冲区
输入参数：
    sockfd：套接字描述符。
    level：选项所在的协议层，常用的是 SOL_SOCKET。
    optname：需要获取的选项名称。
    optval：指向返回选项值的缓冲区。
    optlen：指向包含缓冲区大小的变量。
*/
int getsockopt(int sockfd, int level, int optname, int *optval, socklen_t *optlen) {
  return getsockopt_f(sockfd, level, optname, optval, optlen);
}


/*
setsockopt函数
主要作用：设置指定套接字选项的值，通过 optval 和 optlen 提供的新值更新套接字选项
输入参数：
    sockfd：套接字描述符。
    level：选项所在的协议层。常用值包括 SOL_SOCKET（套接字层），IPPROTO_TCP（TCP 层），IPPROTO_IP（IP 层）等。
    optname：要获取的选项名称。根据 level 的不同，常用选项包括 SO_RCVBUF（接收缓冲区大小），SO_SNDBUF（发送缓冲区大小），SO_RCVTIMEO（接收超时），SO_SNDTIMEO（发送超时）等。
    optval：指向存储选项值的缓冲区。
    optlen：指向存储选项值大小的变量，调用后会更新为实际的选项值大小。
*/
int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen) {
  if (!t_hook_enable) {
    return setsockopt_f(sockfd, level, optname, optval, optlen);
  }
  if (level == SOL_SOCKET) {
    if (optname == SO_RCVTIMEO || optname == SO_SNDTIMEO) {
      FdCtx::ptr ctx = FdMgr::GetInstance()->get(sockfd);
      if (ctx) {
        const timeval *v = (const timeval *)optval;
        ctx->setTimeout(optname, v->tv_sec * 1000 + v->tv_usec / 1000);
      }
    }
  }
  return setsockopt_f(sockfd, level, optname, optval, optlen);
}

}
}
//
// hook的具体实现
// created by magic_pri on 2024-6-27
//

#include "./include/hook.hpp"
#include <dlfcn.h>  // dlsym, RTLD_NEXT
#include <memory>

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

/*
do_io：模板函数
主要作用：封装各种I/O操作，实现超时处理、非阻塞I/O等
*/
template <typename OriginFun, typename... Args>
static ssize_t do_io(int fd, OriginFun fun, const char *hook_fun_name, uint32_t event, int timeout_so, Args &&...args) {
  if (!t_hook_enable) {   // 不使用钩子函数
    return fun(fd, std::forward<Args>(args)...);
  }
  // 为当前文件描述符创建上下文ctx
  FdCtx::ptr ctx = FdMgr::GetInstance()->get(fd);   // FdMgr::GetInstance() 单例实例
  if (!ctx) {
    return fun(fd, std::forward<Args>(args)...);  // 没找到上下文，不使用钩子函数，直接用原始函数执行
  }
  // 文件已经关闭，设置错误码'EBADF'
  if (ctx->isClose()) {
    errno = EBADF;    // 设置全局变量的值为EBADF，表示无效的文件描述符
    return -1;
  }

  if (!ctx->isSocket() || ctx->getUserNonblock()) {
    return fun(fd, std::forward<Args>(args)...);
  }
  // 获取对应type的fd超时时间
  uint64_t to = ctx->getTimeout(timeout_so);
  std::shared_ptr<timer_info> tinfo(new timer_info);

retry:
  ssize_t n = fun(fd, std::forward<Args>(args)...);
  while (n == -1 && errno == EINTR) {
    // 读取操作被信号中断，继续尝试
    n = fun(fd, std::forward<Args>(args)...);
  }
  if (n == -1 && errno == EAGAIN) {
    // 数据未就绪
    IOManager *iom = IOManager::GetThis();
    Timer::ptr timer;
    std::weak_ptr<timer_info> winfo(tinfo);

    if (to != (uint64_t)-1) {
      timer = iom->addConditionTimer(
          to,
          [winfo, fd, iom, event]() {
            auto t = winfo.lock();
            if (!t || t->cnacelled) {
              return;
            }
            t->cnacelled = ETIMEDOUT;
            iom->cancelEvent(fd, (Event)(event));
          },
          winfo);
    }

    int rt = iom->addEvent(fd, (Event)(event));
    if (rt) {
      std::cout << hook_fun_name << " addEvent(" << fd << ", " << event << ")";
      if (timer) {
        timer->cancel();
      }
      return -1;
    } else {
      Fiber::GetThis()->yield();
      if (timer) {
        timer->cancel();
      }
      if (tinfo->cnacelled) {
        errno = tinfo->cnacelled;
        return -1;
      }
      goto retry;
    }
  }

  return n;
}


}
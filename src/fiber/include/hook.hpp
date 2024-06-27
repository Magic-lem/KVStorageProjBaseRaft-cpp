//
// 定义一些函数指针，允许在运行时对系统的一些关键操作进行动态的钩子、替换或跟踪，以实现特定的监控、调试或者控制功能
// 基于函数钩子（hook）的网络编程库
// created by magic_pri on 2024-6-27
//
#ifndef __MONSOON_HOOK_H__
#define __MONSOON_HOOK_H__

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdint.h>

namespace monsoon {
bool is_hook_enable();  // 当前线程是否使用hook
void set_hook_enable(bool flag);  // 设置当前线程是否使用hook
}

// 使用extern "C"声明一系列函数指针，指向不同的系统调用函数或者库函数
extern "C" {
// 睡眠相关函数
// 指向sleep函数的函数指针，用于使当前进程挂起指定的秒数
typedef unsigned int (*sleep_fun)(unsigned int seconds);    // 定义sleep_fun类型，是指向一个函数的指针
extern sleep_fun sleep_f;  // 声明一个全局变量sleep_f，其类型为sleep_fun
// 指向 usleep 函数的函数指针，用于使当前进程挂起指定的微秒数
typedef int (*usleep_fun)(useconds_t usec);
extern usleep_fun usleep_f;
// 指向 nanosleep 函数的函数指针，用于使当前线程挂起指定的纳秒数
typedef int (*nanosleep_fun)(const struct timespec *req, struct timespec *rem);
extern nanosleep_fun nanosleep_f;

// 套接字（socket）相关函数
// 指向 socket 函数的函数指针，用于创建一个套接字
typedef int (*socket_fun)(int domain, int type, int protocol);
extern socket_fun socket_f;
// 指向 connect 函数的函数指针，用于连接到指定的套接字地址
typedef int (*connect_fun)(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
extern connect_fun connect_f;
// 指向 accept 函数的函数指针，用于接受传入的连接请求
typedef int (*accept_fun)(int s, struct sockaddr *addr, socklen_t *addrlen);
extern accept_fun accept_f;

// 读取相关函数
// 指向 read 函数的函数指针，用于从文件描述符读取数据
typedef ssize_t (*read_fun)(int fd, void *buf, size_t count);
extern read_fun read_f;

// 指向 readv 函数的函数指针，用于从文件描述符读取数据到多个缓冲区
typedef ssize_t (*readv_fun)(int fd, const struct iovec *iov, int iovcnt);
extern readv_fun readv_f;

// 指向 recv 函数的函数指针，用于从套接字接收数据。
typedef ssize_t (*recv_fun)(int sockfd, void *buf, size_t len, int flags);
extern recv_fun recv_f;

// 指向 recvfrom 函数的函数指针，用于从指定套接字接收数据，并返回发送方的地址。
typedef ssize_t (*recvfrom_fun)(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr,
                                socklen_t *addrlen);
extern recvfrom_fun recvfrom_f;

// 指向 recvmsg 函数的函数指针，用于从套接字接收多个数据块。
typedef ssize_t (*recvmsg_fun)(int sockfd, struct msghdr *msg, int flags);
extern recvmsg_fun recvmsg_f;

// 写入相关函数
// 指向 write 函数的函数指针，用于向文件描述符写入数据
typedef ssize_t (*write_fun)(int fd, const void *buf, size_t count);
extern write_fun write_f;

// 指向 writev 函数的函数指针，用于从多个缓冲区向文件描述符写入数据
typedef ssize_t (*writev_fun)(int fd, const struct iovec *iov, int iovcnt);
extern writev_fun writev_f;

// 指向 send 函数的函数指针，用于向套接字发送数据
typedef ssize_t (*send_fun)(int s, const void *msg, size_t len, int flags);
extern send_fun send_f;

// 指向 sendto 函数的函数指针，用于向指定套接字地址发送数据
typedef ssize_t (*sendto_fun)(int s, const void *msg, size_t len, int flags, const struct sockaddr *to,
                              socklen_t tolen);
extern sendto_fun sendto_f;

// 指向 sendmsg 函数的函数指针，用于向套接字发送多个数据块
typedef ssize_t (*sendmsg_fun)(int s, const struct msghdr *msg, int flags);
extern sendmsg_fun sendmsg_f;

// 关闭函数
// 指向 close 函数的函数指针，用于关闭文件描述符或者套接字
typedef int (*close_fun)(int fd);
extern close_fun close_f;

// 文件控制和操作函数
// 指向 fcntl 函数的函数指针，用于对文件描述符执行各种控制操作
typedef int (*fcntl_fun)(int fd, int cmd, ... /* arg */);
extern fcntl_fun fcntl_f;

// 指向ioctl函数的函数指针，用于对设备执行各种控制操作
typedef int (*ioctl_fun)(int d, unsigned long int request, ...);
extern ioctl_fun ioctl_f;

// 套接字选项函数
// 指向 getsockopt 函数的函数指针，用于获取套接字选项
typedef int (*getsockopt_fun)(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
extern getsockopt_fun getsockopt_f;

// 指向 setsockopt 函数的函数指针，用于设置套接字选项
typedef int (*setsockopt_fun)(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
extern setsockopt_fun setsockopt_f;

// 带有超时设置的连接操作
extern int connect_with_timeout(int fd, const struct sockaddr *addr, socklen_t addrlen, uint64_t timeout_ms);
}


#endif
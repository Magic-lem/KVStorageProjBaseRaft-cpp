//
// 文件描述符上下文信息、文件句柄类的实现
// created by magic_pri on 2024-6-27
//

#include <sys/stat.h>   // struct stat ，fstat，S_ISSOCK宏
#include <sys/types.h>
#include <unistd.h>
#include "hook.hpp"
#include "fd_manager.hpp"

namespace monsoon {

/*
FdCtx类构造函数
主要功能：初始化各成员变量（上下文信息），调用init函数
*/
FdCtx::FdCtx(int fd): 
        m_isInit(false),
        m_isSocket(false),
        m_sysNonblock(false),
        m_userNoneblock(false),
        m_isClosed(false),
        m_fd(fd),
        m_recvTimeout(-1),
        m_sendTimeout(-1) {
    init();
}

/*
FdCtx类析构函数
*/
FdCtx::~FdCtx() {}

/*
init函数
主要功能：初始化文件描述符（文件句柄）的上下文信息
*/
bool FdCtx::init() {
    // 检查是否已经初始化
    if (m_isInit) {
        return true;    // 已经初始化过了
    }

    m_recvTimeout = -1; // 没有超时限制
    m_sendTimeout = -1;  

    // 获取文件状态信息
    struct stat fd_stat;        // <sys/stat.h>库中的结构体，存储文件的元数据
    if (-1 == fstat(m_fd, &fd_stat)) {  // fstat函数获取文件信息，储存在fd_stat
        // 获取失败
        m_isInit = false;
        m_isSocket = false;
    } else {  
        // 获取成功
        m_isInit = true;
        m_isSocket = S_ISSOCK(fd_stat.st_mode); // S_ISSOC判断是否是socket
    }

    // 如果是socket，则设置非阻塞模式
    if (m_isSocket) {
        int flags = fcntl_f(m_fd, F_GETFL, 0);      // fcntl_f函数指针，获取文件描述符m_fd的标志位
        if (!(flags & O_NONBLOCK)){      // 检查flags是否设置了O_NONBLOCK标志（非阻塞模式）
            // 设置文件描述符为非阻塞模式
            fcntl_f(m_fd, F_SETFL, flags | O_NONBLOCK);
        }
        m_sysNonblock = true;  // 系统非阻塞模式，即通过系统调用（fcntl）实现设置文件非阻塞  
    } else {
        m_sysNonblock = false;  // 不是socket，系统设置为阻塞模式
    }

    m_userNoneblock = false;  // 用户为阻塞模式
    m_isClosed = false;
    return m_isInit;
}

/*
setTimeout函数
设置读或写事件的超时时间
*/
void FdCtx::setTimeout(int type, uint64_t v) {
    if (type == SO_RCVTIMEO) {  // 判断超时事件类型是读还是写
        m_recvTimeout = v;
    } else {
        m_sendTimeout = v;
    }
}

/*
getTimeout函数
获取读或写事件的超时时间
*/
uint64_t FdCtx::getTimeout(int type) {
    if (type == SO_RCVTIMEO) {
        return m_recvTimeout;
    } else {
        return m_sendTimeout;
    }
}


/*
FdManager类构造函数
主要作用：初始化文件句柄集合大小
*/
FdManager::FdManager() { m_datas.resize(64); }

/*
get函数
主要作用：获取或创建文件描述符上下文信息（FdCtx类对象）对象，用指针指向
*/
FdCtx::ptr FdManager::get(int fd, bool auto_create) {
    if (fd == -1) return nullptr;   // 文件描述符是无效值

    // 加读锁，保护共享数据m_datas
    RWMutexType::ReadLock lock(m_mutex);
    if ((int)m_datas.size() <= fd) {  // 文件句柄集合的大小小于文件描述符，说明不存在该文件描述符信息
        if (!auto_create) return nullptr;  // 不创建
    } else if (m_datas[fd] || auto_create) {    // 存在文件描述符或不自动创建，直接返回。（不自动创建，此时有可能还是不存在文件描述符信息，m_datas中有的是空）
        return m_datas[fd];
    }
    lock.unlock();  // 解开读锁

    // 加写锁，保护共享数据m_datas，新创建文件描述符信息
    RWMutexType::WriteLock lock2(m_mutex);
    FdCtx::ptr ctx(new FdCtx(fd));
    if (fd >= (int)m_datas.size()) {
        m_datas.resize(fd * 1.5);  // 集合不够大，扩大1.5倍
    }
    m_datas[fd] = ctx;
    return ctx;
}

/*
del函数
主要作用：删除文件描述符
*/
void FdManager::del(int fd) {
    RWMutexType::WriteLock lock(m_mutex);
    if ((int)m_datas.size() <= fd) {
        return;  // 没有这个文件描述符，不用删除
    }
    m_datas[fd].reset();    // 共享指针重置
}


}
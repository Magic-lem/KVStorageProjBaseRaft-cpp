//
// 文件管理类，文件描述符上下文、句柄类型，阻塞、关闭、读写超时等
// created by magic_pri on 2024-6-7
//
#ifndef __FD_MANAGER_H__
#define __FD_MANAGER_H__

#include <memory>
#include "./include/mutex.hpp"
#include "./include/singleton.hpp"

namespace monsoon {
// 文件描述符上下文信息类，跟踪描述符的状态和属性（如是否已初始化，是否是socket、是否非阻塞、超时时间等）
class FdCtx: public std::enable_shared_from_this<FdCtx> {
public:
    typedef std::shared_ptr<FdCtx> ptr;

    // 构造、析构函数
    FdCtx(int fd);
    ~FdCtx();

    bool isInit() const { return m_isInit; }
    bool isSocket() const { return m_isSocket; }
    bool isClose() const {return m_isClosed; }

    void setUserNonblock(bool v) {  // 用户主动设置是否非阻塞
        m_userNoneblock = v;
    }
    bool getUserNonblock() {    // 查询是否用户主动设置非阻塞
        return m_userNoneblock;
    }

    // 系统非阻塞
    void setSysNoneblock(bool v) {
        m_sysNonblock = v;
    }
    bool getSysNoneblock() {
        return m_sysNonblock;
    }

    // 设置超时时间
    void setTimeout(int type, uint64_t v);
    uint64_t getTimeout(int type);

private:
    bool init();  // 初始化，设为私有成员函数，只有在刚创建类实例的时候调用


private:
    bool m_isInit : 1;  // 是否初始化，占用1 bit
    bool m_isSocket : 1;  // 是否socket
    bool m_sysNonblock : 1;  // 是否hook非阻塞
    bool m_userNoneblock : 1; // 是否用户主动设置非阻塞
    bool m_isClosed : 1;  // 是否关闭
    int m_fd;   // 文件描述符
    uint64_t m_recvTimeout;   // 读超时时间（单位：ms）
    uint64_t m_sendTimeout;   // 写超时事件（单位：ms）
};

// 文件描述符上下文管理类
class FdManager {
public:
    typedef RWMutex RWMutexType;  // 读写锁

    FdManager();
    FdCtx::ptr get(int fd, bool auto_create = false);   // 获得文件描述符上下文信息类，如果没有，根据auto_create是否创建
    void del(int fd);  // 删除文件描述符

private:
    RWMutexType m_mutex;  // 读写锁
    std::vector<FdCtx::ptr> m_datas;  // 文件句柄集合
};

typedef Singleton<FdManager> FdMgr; // 单例模式封装FdManager类

}
#endif
//
// 线程类的定义，封装pthread中的线程功能
// created by magic_pri on 2024-6-24
//

#ifndef __MONSOON_THREAD_H__
#define __MONSOON_THREAD_H__

#include <sys/types.h>    // pit_t  进程ID
#include <pthread.h>
#include <functional>
#include <string>
#include <memory>

namespace monsoon {
/*
Thread：线程类
主要作用：封装pthread库中的线程功能，使得能够满足RAII模式，正确管理线程
*/
class Thread {
public:
    typedef std::shared_ptr<Thread> ptr;    // 指向Thread对象的共享指针

    // 构造函数
    Thread(std::function<void()> cb, const std::string &name);
    ~Thread();

    pid_t getId() const { return id_; }
    const std::string &getName() const { return name_; }
    void join();   // 阻塞主线程以等待线程执行完毕

    // 静态成员函数
    static Thread *GetThis();   // 返回指向当前线程的'Thread'实例的指针
    static const std::string &GetName();  // 当前线程的名称
    static void SetName(const std::string &name);  // 设置当前线程的名称
  

private:    // 私有成员函数
    Thread(const Thread &) = delete;    // 禁止拷贝构造
    Thread(const Thread &&) = delete;   // 禁止移动构造
    Thread operator=(const Thread &) = delete;   // 禁止赋值操作符

    static void *run(void *args);  // 线程的执行函数，私有，只能在类内部使用

private:
    pid_t id_;  // 线程所在的进程ID
    pthread_t thread_;  // POSIX线程对象
    std::function<void()> cb_;   // 线程所执行的函数对象
    std::string name_;   // 线程名称
};
}
#endif
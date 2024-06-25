//
// 协程调动相关功能
// create by magic_pri on 2024-6-24
//

#ifndef __MONSOON_SCHEDULER_H__
#define __MONSOON_SCHEDULER_H__

#include "fiber.hpp"
#include "thread.hpp"
#include "mutex.hpp"  
#include <string>
#include <vector>
#include <list>
#include <atomic>

namespace monsoon {

/*
SchedulerTask：调度任务类
主要作用：表示一个调度任务，这个任务可以是一个协程对象或者一个函数对象
*/
class SchedulerTask {
public:
    SchedulerTask() {   
        thread_ = -1;   // 初始化为-1
    }

    SchedulerTask(Fiber::ptr f, int t) {    // 含参构造函数，调度任务为协程对象
        fiber_ = f;
        thread_ = t;
    }

    SchedulerTask(std::function<void()> f, int t) {     // 含参构造函数，调度任务为函数对象
        cb_ = f;
        thread_ = t;
    }

    // 清空任务
    void reset() {
        fiber_ = nullptr;
        cb_ = nullptr;
        thread_ = -1;
    }

private:
    Fiber::ptr fiber_;   // 指向协程对象的指针
    std::function<void()> cb_;    // 回调函数
    int thread_;   // 执行该任务的线程，-1表示不指定线程
};


/*
Scheduler：N-M协程调度器，管理多线程和协程
主要作用：
    用于管理多个线程和协程
*/
class Scheduler {
public:
    typedef std::shared_ptr<Scheduler> ptr;    // Scheduler::ptr 是Scheduler类对象的共享指针类型的别名

    Scheduler(size_t threads = 1, bool user_caller = true, const std::string &name = "Scheduler");  
    virtual ~Scheduler();   // 析构函数定义为虚函数

    const std::string &getName() const { return name_; }

    // 静态成员函数
    static Scheduler *GetThis();    // 获得当前线程的协程调度器
    static Fiber *GetMainFiber();   // 获得当前线程的主协程

    // 添加任务到调度器中
    template <class TaskType>
    void scheduler(TaskType task, int thread = -1) {
        bool isNeedTickle = false;    // 是否需要唤醒空闲的协程
        {
            Mutex::Lock lock(mutex_);   // 初始化局部互斥锁lock，此时会自动加锁（ScopedLockImpl类中的定义）
            // 因此这里已经加了锁，下面添加调度任务就不需要在加锁了
            isNeedTickle = schedulerNoLock(task, thread);   // 添加调度任务
        }

        if (isNeedTickle) {
            tickle();
        }
    }

    void start();   // 启动调度器
    void stop();   // 停止调度器，等待所有任务结束

protected:
    virtual void tickle();   // 通知调度器任务到达
    void run();   // 协程调度函数
    virtual void idle();   // 无任务时，执行idle协程
    virtual bool stopping();   // 返回是否可以停止
    void setThis();    // 设置当前线程调度器
    bool isHasIdleThreads() {   // 有没有空闲进程
        return idleTreadCnt_  > 0;
    }


private:
    // 满足无锁条件时（确保task_没被其他线程加锁占用），添加调度任务
    // TODO：加入使用clang的锁检查
    template <class TaskType>
    bool schedulerNoLock(TaskType t, int thread) {
        bool isNeedTickle = tasks_.empty();   // 任务队列是空，可以唤醒空闲的协程
        SchedulerTask task(t, thread);
        if (task.fiber_ || task.cb_) {  // 要么是协程，要么是函数对象
            tasks.push_back(task);   // 任务有效，加入到任务队列中
        }
        return isNeedTickle;
    }

    std::string name_;  // 调度器名称
    Mutex mutex_;   // 互斥锁
    std::vector<Thread::ptr> threadPool_;  // 线程池
    std::list<SchedulerTask> tasks_;  // 任务队列
    std::vector<int> threadIds_;   // 线程池ID数组
    size_t threadCnt_ = 0;   // 工作线程数量（不包含主线程）
    std::atomic<size_t> activeThreadCnt_ = {0};   // 活跃线程数目
    std::atomic<size_t> idleTreadCnt_ = {0};  // IDL线程（在线程池中处于空闲的线程）数目
    Fiber::ptr rootFiber_;   // 指向本线程的调度协程（主协程）
    bool isUseCaller_;   // 是否使用use_caller模式
    bool isStopped_;   // 线程池或调度器是否已经停止

    // 以下变量仅在use_caller模式中有用
    Fiber::ptr fiber_;   // 存储调度器协程所在线程（主线程）的协程（因为use_caller模式，主线程也会执行除管理线程池或调度器以外的任务）
    int rootThread_ = 0;   // 存储调度器协程所在的线程（主线程）ID

};


}

#endif
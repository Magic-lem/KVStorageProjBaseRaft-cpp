// 
// 简单的协程（轻量级线程）类，用于多任务机制
// created by magic_pri on 2024-6-22
//

#ifndef __MONSOON_FIBER_H__
#define __MONSOON_FIBER_H__

#include <memory>  // 智能指针
#include <functional>   // std::function<T>
#include <ucontext.h>   // ucontext_t

// 所有代码都定义在了'monson'命名空间中，避免与其他代码产生命名冲突
namespace monsoon {

/*
Fiber类的声明
主要作用：
    1. 提供了一些基础功能来管理和调度协程的执行
*/
class Fiber : public std::enable_shared_from_this<Fiber> {   // std::enable_shared_from_this<T> C++标准库模板类，用于启用从类的成员函数生成共享指针的功能
public:
    typedef std::shared_ptr<Fiber> ptr;    // 指向Fiber类对象的共享指针
    // Fiber状态机
    enum State {
        READY,    // 就绪态，刚创建或者从'yield'中恢复，准备运行
        RUNNING,  // 运行态，协程正在运行
        TERM,     // 结束态，协程的回调函数执行完毕，已经结束
    };

private:
    // 私有的默认构造函数，用来在GetThis中调用以初始化主协程，不能显式调用
    Fiber();

public:
    // 含参构造函数，用于构造子协程，指定协程的回调函数和栈大小
    Fiber(std::function<void()> cb, size_t stackSz = 0, bool run_in_scheduler = true);  
    ~Fiber();

    void reset(std::function<void()> cb);  // 重置协程状态，复用栈空间。（给协程重新绑定一个回调函数，在同一个空间但是功能改变了）
    void resume();   // 切换协程到运行态
    void yield();   // 让出协程执行权
    uint64_t getId() const { return id_; }   // 获得协程ID
    State getState() const { return state_; }   // 活动协程状态

    // 静态成员函数
    static void SetThis(Fiber *f);   // 设置当前正在运行的协程
    static Fiber::ptr GetThis();   // 获取当前线程中的执行协程，如果当前线程没有创建协程，则创建一个且作为主协程
    static uint64_t TotalFiberNum();   // 获得协程总数
    static void MainFunc();    // 协程执行函数
    static uint64_t GetCurFiberID();   // 获得当前协程ID


private:
    uint64_t id_ = 0;  // 协程ID
    uint32_t stackSize_ = 0;  // 协程栈大小
    State state_ = READY;   // 协程状态
    ucontext_t ctx_;  // 协程的上下文
    void *stack_ptr = nullptr;    // 协程栈地址
    std::function<void()> cb_;   // 协程回调函数
    bool isRunInscheduler_;   // 本协程是否参与调度器调度
};
}


#endif
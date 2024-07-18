//
// 协程类的具体实现
// created by magic_pri on 2024-6-23
//

#include "fiber.hpp"
#include "utils.hpp"
#include "scheduler.hpp"
#include <atomic>

namespace monsoon {
const bool DEBUG = true; 

// 线程中的协程管理：一些全局变量，存储实例
static thread_local Fiber *cur_fiber = nullptr;   // 当前线程正在运行的协程
static thread_local Fiber::ptr cur_thread_fiber = nullptr;   // 当前线程的主协程
static std::atomic<uint64_t> cur_fiber_id{0};   // 用于生成协程的ID，初始化为0
static std::atomic<uint64_t> fiber_count{0};    // 统计当前协程数目
static int g_fiber_stack_size = 128 * 1024;    // 协程栈的默认大小为128KB

/*
辅助类：StackAllocator
功能：提供一个抽象层来管理协程栈的内存分配和释放。
方法：提供静态方法用于分配和释放内存，方便在任何地方调用
*/
class StackAllocator {
public:
    static void *Alloc(size_t size) { return malloc(size); }
    static void Delete(void *vp, size_t size) { return free(vp); }
};


// Fiber类的实现 👇

/*
私有的默认构造函数
功能：初始化一个主协程
主协程是每个线程启动时的第一个协程，通常用于保存线程的初始上下文，以便其他协程可以在需要时切换回主协程。
主协程通常没有回调函数，因为不是面向特定的任务
主协程应始终处于RUNNING状态
*/
Fiber::Fiber() {
    SetThis(this);  // 将当前协程运行，为主协程
    state_ = RUNNING;
    CondPanic(getcontext(&ctx_) == 0, "getcontext error");   // 获取当前协程的上下文，保存在ctx_中。如果出错，会触发CondPanic
    ++fiber_count;
    id_ = cur_fiber_id++;   // 协程的id被设为cur_fiber_id，cur_fiber_id自增，存储下一个id
    std::cout << "[fiber] create fiber, id = " << id_ << std::endl;
}

/*
含参构造函数
功能：初始化一个子协程，指定协程的回调函数和栈大小
Input：std::function<void()> cb  协程的回调函数
       size_t stacksize   协程的栈空间大小
       bool run_in_scheduler   是否参与调度器调度
*/
Fiber::Fiber(std::function<void()> cb, size_t stacksize, bool run_in_scheduler)
        : id_(cur_fiber_id++), cb_(cb), isRunInscheduler_(run_in_scheduler) {
    ++fiber_count;
    stackSize_ = stacksize > 0 ? stacksize : g_fiber_stack_size;
    stack_ptr = StackAllocator::Alloc(stackSize_);   // 基于协程的占空间大小为协程分配内存
    // 获得协程的上下文
    CondPanic(getcontext(&ctx_) == 0, "getcontext error");
    // 初始化协程上下文的上下文指针、栈消息
    ctx_.uc_link = nullptr;
    ctx_.uc_stack.ss_sp = stack_ptr;    // 指向栈空见的指针
    ctx_.uc_stack.ss_size = stackSize_;
    makecontext(&ctx_, &Fiber::MainFunc, 0);   // 修改上下文，能够执行指定的协程执行函数
}


/*
void SetThis(Fiber *f)
功能：设置当前正在运行的协程
Input：Fiber *f，正在运行的协程
*/
void Fiber::SetThis(Fiber *f) {
    cur_fiber = f;
}

/*
Fiber::ptr GetThis()
功能：获取线程中正在运行的协程，如果不存在则创建主协程
*/
Fiber::ptr Fiber::GetThis() {
    if (cur_fiber) return cur_fiber->shared_from_this();   // 返回指向该协程的共享指针
    // 如果不存在正在运行的协程，则创建主协程
    Fiber::ptr main_fiber(new Fiber);   // 使用默认构造函数构造了main_fiber
    CondPanic(cur_fiber == main_fiber.get(), "cur_fiber need to be main_fiber");
    cur_thread_fiber = main_fiber;
    return cur_fiber->shared_from_this();
}

/*
void Fiber::resume()
功能：切换当前协程到运行态，保存主协程的上下文
*/
void Fiber::resume() {
    CondPanic(state_ != RUNNING && state_ != TERM, "state error");  // 确保当前协程处于就绪态，否则断言错误
    SetThis(this);
    state_ = RUNNING;

    // 执行协程的上下文切换
    if (isRunInscheduler_) {
        // 如果是参与调度器调度，则与调度器进行上下文的切换
        CondPanic(0 == swapcontext(&ctx_, &(Scheduler::GetMainFiber()->ctx_)), 
                  "isRunInScheduler_ = true, swapcontext error");
    } else {
        // 否则，与主协程进行上下文的切换
        CondPanic(0 == swapcontext(&ctx_, &cur_thread_fiber->ctx_), 
                  "isRunInScheduler_ = false, swapcontext error");
    }
}

/*
void Fiber::yield()
功能：让出协程的执行权
*/
void Fiber::yield() {
    CondPanic(state_ == RUNNING || state_ == TERM, "state error");  // 状态应该是运行中或者已结束
    SetThis(cur_thread_fiber.get());   // 将运行权还给主协程
    if (state_ != TERM) {
        // 协程还没执行完毕，设为就绪态等待下次执行
        state_ = READY;
    }
    if (isRunInscheduler_) {
        CondPanic(0 == swapcontext(&ctx_, &Scheduler::GetMainFiber()->ctx_), 
                  "isRunInScheduler_ = true, swapcontext error");
    } else {
        CondPanic(0 == swapcontext(&ctx_, &cur_thread_fiber->ctx_), 
                  "isRunInScheduler_ = false, swapcontext error");
    }
}

/*
void reset(std::function<void()> cb)
功能：重置协程状态，复用栈空间
Input：std::function<void> cb 将要新绑定的回调函数
*/
void Fiber::reset(std::function<void()> cb) {
    CondPanic(stack_ptr, "stack is nullptr");  // 需要有地址
    CondPanic(state_ == TERM, "state is not Term");  // 需要是已经执行完成的协程
    cb_ = cb;  // 更换回调函数
    CondPanic(0 == getcontext(&ctx_), "getcontext error");   // 获取新的上下文
    // 重新初始化上下文信息
    ctx_.uc_link = nullptr;
    ctx_.uc_stack.ss_sp = stack_ptr;
    ctx_.uc_stack.ss_size = stackSize_;

    makecontext(&ctx_, &Fiber::MainFunc, 0);    // 初始化上下文，指定协程执行函数
    state_ = READY;
}

/*
void Fiber::MainFunc()
功能：协程的入口函数
步骤：
    1. 获取当前协程
    2. 执行协程的回调函数
    3. 执行完毕后，清理回调函数，将状态设为TERM
    4. 释放指针
    5. 让出执行权
*/
void Fiber::MainFunc() {
    Fiber::ptr cur = GetThis();
    CondPanic(cur != nullptr, "cur is nullptr");

    // 执行回调函数
    cur->cb_();
    
    // 执行完毕
    cur->cb_ = nullptr;
    cur->state_ = TERM;
    // 释放共享指针cur，使得引用计数-1
    auto raw_ptr = cur.get();
    cur.reset();
    // 协程结束，让出执行权
    raw_ptr->yield();
}

/*
uint64_t GetCurFiberID()
功能：获得当前正在运行的协程的ID
注意：此函数为静态成员函数，与对象无关，所以直接去找当前正在执行的协程
*/
uint64_t Fiber::GetCurFiberID() {

    if (cur_fiber) {
        return cur_fiber->getId();   // 返回当前协程的ID
    }

    return 0;  // 没有正在运行的协程
}

/*
uint64_t TotalFiberNum()
功能：获得当前协程的数量
*/
uint64_t Fiber::TotalFiberNum() {
    return fiber_count.load();   // 以原子的方式读取std::atomic的值
}

/*
析构函数
减少协程的计数
要注意由于协程可能有栈空间（是在堆区动态分配的），要注意释放
主协程没有栈空间
*/
Fiber::~Fiber() {
    --fiber_count;

    // 如果有栈空间
    if (stack_ptr != nullptr) {
        CondPanic(state_ == TERM, "fiber state should be term");
        StackAllocator::Delete(stack_ptr, stackSize_);  // 释放空间
    } else {
        // 没有栈空间，是主协程
        CondPanic(!cb_, "main fiber should no callback");  // 主协程不应该有回调函数
        CondPanic(state_ == RUNNING, "main fiber state should be RUNNING");  // 主协程应该保持运行状态

        // 检查当前执行的协程是否为主协程，如果是，则当前执行的协程指针设为nullptr（使用SetThis），表示已经没有执行的协程了，避免悬空指针
        Fiber *cur = cur_fiber;
        if (cur == this) {
            SetThis(nullptr);
        }
    }
}


}
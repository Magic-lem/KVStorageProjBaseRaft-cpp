// 
// Scheduler调度器类的具体实现
// created by magic_pri on 2024-6-25
//

#include "./include/mutex.hpp"
#include "./include/scheduler.hpp"
#include "./include/utils.hpp"
#include "./include/hook.hpp"

namespace monsoon {
// 静态全局变量，记录调度器实例等
static thread_local Scheduler *cur_scheduler = nullptr;    // 指向当前线程的调度器
static thread_local Fiber *cur_scheduler_fiber = nullptr;    // 指向当前线程中调度器所在的协程（又称为调度协程、即主协程）

const std::string LOG_HEAD = "[scheduler] ";   // 日志头，用来表明输出日志是由scheduler输出的

/*
Scheduler 构造函数
主要功能：初始化调度器和主线程，若是use_caller模式同时还初始化caller线程的主协程
Input: size_t threads 总线程数
       bool use_caller 是否是use_caller模式
       const std::string &name  调度器的名称
*/
Scheduler::Scheduler(size_t threads, bool use_caller, const std::string &name){
    CondPanic(threads > 0, "thread <= 0");
    name_ = name;

    if (use_caller) {   // use_caller模式，当前线程也作为被调度的线程
        std::cout << LOG_HEAD << "current thread as called thread" << std::endl;
        --threads;   // 工作线程总数-1（工作线程不包括主线程，但主线程占了一个位置）

        // 初始化caller线程的主协程
        Fiber::GetThis();   // 然后，Fiber::cur_thread_fiber即可获取初始化后的主协程
        std::cout << LOG_HEAD << "init caller thread's main fiber success" << std::endl;

        // 确保当前线程没有调度器实例，避免冲突（每个线程只能有一个调度器）
        CondPanic(GetThis() == nullptr, "GetThis err: cur scheduler is not nullptr");

        // 设置为当前线程的调度器实例
        cur_scheduler = this;

        // 创建调度协程rootFiber_，其任务为执行Scheduler的run方法（即运行调度器，因此是调度协程）
        rootFiber_.reset(new Fiber(std::bind(&Scheduler::run, this), 0, false));   // reset为shared_ptr的方法，替换指针托管的对象
        std::cout << LOG_HEAD << "init caller thread's caller fiber success" << std::endl;

        Thread::SetName(name_);
        cur_scheduler_fiber = rootFiber_.get(); // 智能指针到裸指针，要加个get
        rootThread_ = GetThreadId();    // 获得线程ID
        threadIds_.push_back(rootThread_);
    } else {    // 非user_caller模式，当前线程不会被调度
        rootThread_ = -1;   //   主线程不会被调度，用-1标记主线程ID
    }
    threadCnt_ = threads;
    std::cout << "-------scheduler init success-------" << std::endl;
}

/*
GetThis 静态成员函数
主要功能：能够用类直接获得当前线程的调度器
*/
Scheduler *Scheduler::GetThis() {
    return cur_scheduler;   // 是当前线程的调度器，所以用这个全局变量
}

/*
GetMainFiber 静态成员函数
主要功能：直接用类获得当前线程的主协程
*/
Fiber *Scheduler::GetMainFiber() {
    return cur_scheduler_fiber;
}


/*
setThis：设置当前线程的调度器
主要功能：将当前线程的调度器设为本调度器实例
注意事项：此函数为protected级别，不允许外部对象直接访问，通常是在类函数中使用，保证只有调度器类或派生类能修改线程的调度器
*/
void Scheduler::setThis() {
    cur_scheduler = this;
}

/*
start：启动调度器，提供给外部的接口
主要功能：启动调度器，创建并初始化线程池，使调度器能够开始执行调度任务
*/
void Scheduler::start() {
    std::cout << LOG_HEAD << "scheduler start" << std::endl;
    // 添加局部互斥锁，保护共享资源
    Mutex::Lock lock(mutex_);
    // 检查调度器是否已经停止，如果停止则直接返回
    if (isStopped_) {
      std::cout << "scheduler is stopped" << std::endl;
      return;
    }
    // 确保线程池应该是空的
    CondPanic(threadPool_.empty(), "thread pool should be empty");
    threadPool_.resize(threadCnt_);   // 根据线程数量确定线程池的大小
    
    // 创建线程，加入到线程池
    for (size_t i = 0; i < threadCnt_; ++i) {
      threadPool_[i].reset(new Thread(std::bind(&Scheduler::run, this), name_ + "_" + std::to_string(i)));  // 创建线程并执行
      threadIds_.push_back(threadPool_[i]->getId());
    }
}

/*
run：调度器开始调度任务
主要功能：负责管理和执行调度器的任务，包括：管理任务队列、调度和执行任务、处理空闲状态等
*/
void Scheduler::run() {
    std::cout << LOG_HEAD << "begin run" << std::endl;
    set_hook_enable(true);   // 启用hook
    setThis();  
    if (GetThreadId() != rootThread_) { // 当前线程不是caller线程（因为caller线程的主协程（调度协程）已经初始化过了)
        // 初始化主协程
        cur_scheduler_fiber = Fiber::GetThis().get();
    }

    // 创建idle协程，用于空闲时执行idle函数
    Fiber::ptr idleFiber(new Fiber(std::bind(&Scheduler::idle, this)));
    Fiber::ptr cbFiber;  // 可重用的协程，用于执行函数对象（任务是个函数而不是协程时，使用cbFiber运行）

    SchedulerTask task;  // 任务
    while (true) {  // 调度器将在这个循环中不断调度和执行任务，直到外部条件中断
        task.reset();   // 重置任务，确保每一轮开始任务是空的
        bool tickle_me;  // 用来标记是否需要通知线程进行调度的变量
        {
            Mutex::Lock lock(mutex_);
            auto it = tasks_.begin();  
            // 遍历所有任务
            while (it != tasks_.end()) {
                if (it->thread_ != -1 && it->thread_ != GetThreadId()) {    // 当任务已经指定了线程，且不是本线程，跳过，并通知其他线程要调度该任务
                    ++it;
                    tickle_me = true;
                    continue;
                }
                // 验证任务是否正确封装了协程或者函数
                CondPanic(it->fiber_ || it->cb_, "task is nullptr");
                if (it->fiber_) {   // 协程还要验证下状态
                    CondPanic(it->fiber_->getState() == Fiber::READY, "fiber task state error");
                }

                // 找到一个可行的任务，准备开始调度
                task = *it;
                tasks_.erase(it++); // 从任务队列中删除
                ++activeThreadCnt_;  // 增加活跃线程的数量
                break;
            }
            // 当前线程取出任务后，如果还存在任务，则通知其他线程
            tickle_me |= (it != tasks_.end());
        }
        if (tickle_me) {
            tickle();   // 通知线程还有任务
        }

        // 开始执行任务
        if (task.fiber_) {  // 执行协程
            task.fiber_->resume();  //切换到运行态，开始运行
            // 执行结束
            --activeThreadCnt_;
            task.reset();  // 清空任务，再次循环
        } else if (task.cb_) {
            if (cbFiber) {
                cbFiber->reset(task.cb_);   // Fiber对象的重置，指针指向位置不变
            } else {
                cbFiber.reset(new Fiber(task.cb_)); // 共享指针的重置，指向一个新Fiber对象
            }
        } else {
            // 没有任务，task为空，执行idle协程
            if (idleFiber->getState() == Fiber::TERM) { // 但是idle协程结束了
                std::cout << LOG_HEAD << "idle fiber term" << std::endl;
                break; 
            }
            // idle协程正常时，不断空轮转(idle协程执行->idle协程yield挂起->进入调度器主循环->没有任务->idle协程执行)
            ++idleTreadCnt_;    // 增加空闲线程数量，表示本线程为空闲
            idleFiber->resume();    // 空闲期间执行dile协程
            --idleTreadCnt_;    // idle协程退出，此线程开始新的循环寻找任务
        }
    }
    std::cout << "run exit" << std::endl;   // 触发了打破循环的条件，调度器退出运行
}

/*
tickle：通知还有任务/任务到达
TODO：是个虚函数，并没有具体实现。可以使用条件变量实现
*/
void Scheduler::tickle() {
    std::cout << "tickle" << std::endl;
}

/*
stopping：判断是否可以停止调度器
*/
bool Scheduler::stopping() {
    Mutex::Lock lock(mutex_);   // 加锁，访问共享变量
    return isStopped_ && tasks_.empty() && activeThreadCnt_ == 0;
}

/*
idle：空闲协程所执行的函数
主要功能：使线程处于等待状态，直到有新的任务
*/
void Scheduler::idle() {
    while (!stopping()) {
        Fiber::GetThis()->yield();   // 空闲协程让出执行权限给调度协程，调度协程会接着之前的上下文继续执行，从而如果有新任务到来调度器能更快的发现
    }

    // 疑问：直接执行完不也是回到调度协程吗？
    // 解答：与执行完不同的是，空闲协程让出执行权限时，仍然是位于while循环中，所以当重新又启动协程还是在while循环中，而不是重新执行idle函数
    //      也就是说，空闲协程是一直在执行这个函数的，只不过让出权限后暂停了，这个函数并没有运行完
}

/*
stop：停止调度器
主要作用：
*/
void Scheduler::stop() {
    std::cout << LOG_HEAD << "stop" << std::endl;
    if (stopping()) return;     // 已经处于停止状态了
    isStopped_ = true;  // 设置停止标志

    // 对于use_caller模式，stop只能由caller线程执行
    if (isUseCaller_) {
        // 疑问？ 为什么这样能保证是caller线程
        CondPanic(GetThis() == this, "cur thread is not caller thread");    // caller线程是管理调度器的
    } else {
        CondPanic(GetThis() != this, "cur thread is work thread");      // 
    }

    for (size_t i = 0; i < threadCnt_; i++) {
        tickle();   // 唤醒线程从任务对列中获取任务执行
    }
    if (rootFiber_) {
        tickle();
    }

    // 在use_caller情况下，应当唤醒调度器协程（rootFiber），以便调度器正确终止。结束后，应该返回调度协程(caller协程，即当前)
    if (rootFiber_) {
        rootFiber_->resume();
        std::cout << "root fiber << end" << std::endl;
    }

    // 等待所有线程退出
    std::vector<Thread::ptr> threads;
    {
        Mutex::Lock lock(mutex_);   
        threads.swap(threadPool_);  // 从线程池移动到threas中，清空线程池
    }

    for (auto &i: threads) {
        i->join();  // 等待所有线程执行完毕
    }

}

/*
析构函数
主要功能：删除当前线程的调度器
*/
Scheduler::~Scheduler() {
    CondPanic(isStopped_, "isstopped is false");  // 检查调度器是否已经停止，停止了才会继续析构
    if (GetThis() == this) {    // 如果当前线程的调度器是本调度器，则删除
        cur_scheduler = nullptr;
    }
}


}
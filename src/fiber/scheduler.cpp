// 
// Scheduler调度器类的具体实现
// created by magic_pri on 2024-6-25
//

#include "./include/mutex.hpp"
#include "./include/scheduler.hpp"
#include "./include/utils.hpp"

namespace monsoon {
// 静态全局变量，记录调度器实例等
static thread_local Scheduler *cur_scheduler = nullptr;    // 指向当前线程的调度器，每个线程都会指向一个调度器，可能存在多个线程指向同一个调度器（共享一个调度器），实现调度器分配和调度协程到不同的线程上执行
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
      threadPool_[i].reset(Thread(std::bind(&Scheduler::run(), this), name_ + "_" + std::to_string(i)));
      threadIds_.push_back(threadPool_[i]->getId());
    }
}

/*
run：调度器开始调度任务
主要功能：负责管理和执行调度器的任务，包括：管理任务队列、调度和执行任务、处理空闲状态等
*/
scheduler::run() {
  std::cout << LOG_HEAD << "begin run" << std::endl;
  set_hook_enable(true);   // TODO
  setThis();  
  if (GetThreadId() != rootThread_) { // 当前线程不是caller线程（因为caller线程的主协程（调度协程）已经初始化过了
    // 初始化主协程
    cur_thread_fiber = Fiber::GetThis().get();
  }

  // 创建idle协程，用于空闲时执行idle函数
  Fiber::ptr idleFiber(new Fiber(std::bind(&Scheduler::idle, this)));
  Fiber::ptr cbFiber;  // 可重用的协程，用于执行任务回调


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
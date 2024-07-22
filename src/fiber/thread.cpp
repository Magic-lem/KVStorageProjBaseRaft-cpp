//
// 线程类的具体实现
// created by magic_pri on 2024-6-24
//

#include "thread.hpp"
#include "utils.hpp"

namespace monsoon {
// 全局变量，指向当前的线程
static thread_local Thread *cur_thread = nullptr;
static thread_local std::string cur_thread_name  = "UNKNOW";

/*
Thread::Thread(std::function<void()> cb, const std::string &name) 含参构造函数
主要作用：初始化线程函数，创建新线程
Input:  std::function<void()> cb 函数对象
        const std::string &name  线程名称
*/
Thread::Thread(std::function<void()> cb, const std::string &name = "UNKNOW"): cb_(cb), name_(name) {
    if (name.empty()) name_ = "UNKNOW";

    // 创建一个新的线程，请注意：此时线程将直接开始运行，执行run函数!!!!!
    int rt = pthread_create(&thread_, nullptr, &Thread::run, this); // this是传给run的参数，是指向当前线程的指针。此时会开始执行run
    if (rt) {   // 创建失败
        std::cout << "pthread_create error, name: " << name_ << std::endl;
        throw std::logic_error("pthread_create");   // 抛出异常
    }
}

/*
void* Thread::run(void *args)
主要作用：静态成员函数，线程要执行的函数，定义线程的执行内容
Input：void *args  pthread_create(&thread_, nullptr, &Thread::run, this)中的this
*/
void *Thread::run(void *arg) {
    // 将arg转换回Thread*类型的指针
    Thread *thread = static_cast<Thread *>(arg);  // 类型转换
    cur_thread = thread;   // 修改当前执行的线程
    cur_thread_name = thread->name_;

    // 给线程命名
    pthread_setname_np(pthread_self(), thread->name_.substr(0, 15).c_str());    // 名称长度限制为15字符
    std::function<void()> cb;   
    cb.swap(thread->cb_);   // 交换回调函数，防止多次调用、保证线程安全、资源释放
    cb();
    return nullptr;
}

/*
~Thread：析构函数
主要功能：确保线程在对象销毁时不会阻塞或资源泄漏，正确管理线程资源
*/
Thread::~Thread() {
    if (thread_) {
        pthread_detach(thread_);    // 分离线程，则该线程的资源会在线程终止时自动释放
    }
}

/*
void Thread::join()：阻塞等待
主要功能：阻塞当前线程，等待目标线程终止，释放资源
*/
void Thread::join() {
    if (thread_) {
        int rt = pthread_join(thread_, nullptr);   // 等待线程终止，不获得线程返回值
        if (rt) {  // 失败
            std::cout << "pthread_join error, name: " << name_ << std::endl;
            throw std::logic_error("pthread_join");
        }
        thread_ = 0;    // 线程已经结束，使用标记
    }
}

/*
static Thread *GetThis()：静态成员函数
主要功能：获取当前线程
*/
Thread *Thread::GetThis() {
    return cur_thread;
}

/*
static const std::string &Thread::GetName()：静态成员函数
主要功能：获取当前线程的名称
*/
const std::string &Thread::GetName() {
    return cur_thread_name;
}

/*
static void SetName(const std::string &name)：静态成员函数
主要功能：设置当前线程的名称
*/
void Thread::SetName(const std::string &name) {
    if (name.empty()) return;

    if (cur_thread) {
        cur_thread->name_ = name;
    }

    cur_thread_name = name;
}


}
//
// 局部互斥锁、读写锁声明和实现，基于RAII原则，封装pthread库中的锁
// created by magic_pri on 2024-6-24
//

#ifndef __MONSOON_MUTEX_H__
#define __MONSOON_MUTEX_H__

#include "noncopyable.hpp"
#include "utils.hpp"
#include <stdint.h>   // uint32_t
#include <semaphore.h>  // sem_t
#include <iostream>
#include <pthread.h> 

namespace monsoon {

/*
Semaphore：信号量类
主要功能：同步多个线程对贡献资源的访问
TODO：仅声明，具体尚未实现
*/
class Semaphore : Nonecopyable {    // 继承Nonecopyable，确保对象无法通过拷贝构造函数和赋值构造新实例
public:
    Semaphore(uint32_t count = 0);
    ~Semaphore();

    void wait();    // P操作
    void notify();  // V操作

private:
    sem_t semaphore_;
};


/*
ScopedLockImpl：局部互斥锁类模板
主要作用：实现互斥锁的自动管理，即RAII（资源获取即初始化）
*/
template <class T>
struct ScopedLocalImpl{
public:
    // 构造函数
    ScopedLocalImpl(T &mutex): m_(mutex) {
        mutex_.lock();
        isLocked_ = true;
    }
    
    void lock() {   // 加锁
        if (!isLocked_) {
            std::cout << "lock" << std::endl;
            mutex_.lock();
            isLocked_ = true;
        }
    }

    void unlock() { // 解锁
        if (isLocked_) {
            std::cout << "unlock" << std::endl;
            mutex_.unlock();
            isLocked_ = false;
        }
    }

    // 析构函数
    ~ScopedLocalImpl() {
        unlock();   // 要确保解锁
    }

private:
    T &mutex_;      // 锁，是一个引用/别名，而不需要新开辟内存
    bool isLocked_; // 记录是否已经上锁
};

/*
ReadScopedLockImpl：读锁模板类
主要作用：实现读锁的自动管理
*/
template<class T>
struct ReadScopedLockImpl {
public:
    ReadScopedLockImpl(T &mutex): mutex_(mutex) {
        mutex_.rdlock();  // 加读锁
        isLocked_ = true;
    }

    void lock() {
        if (!isLocked_) {
            mutex_.rdlock();
            isLocked_ = true;
        }
    }

    void unlock() {
        if (isLocked_) {
            mutex_.unlock();
            isLocked_ = false;
        }
    }

    ~ReadScopedLockImpl() { unlock(); }

private:
    T &mutex_;  
    bool isLocked_;
};

/*
WriteScopedLockImpl：局部写锁类模板
主要作用：实现写锁的自动管理
*/
template<class T>
struct WriteScopedLockImpl {
public:
    WriteScopedLockImpl(T &mutex): m_(mutex) {
        mutex_.wrlock();
        isLocked_ = true;
    } 

    void lock() {
        if (!isLocked_) {
            mutex_.wrlock();
            isLocked_ = true;
        }
    }

    void unlock() {
        if (isLocked_) {
            mutex_.unlock();
            isLocked_ = false;
        }
    }

    ~WriteScopedLockImpl() {
        unlock();
    }

private:
    T &mutex_;
    bool isLocked_;
};

/*
Mutex：互斥锁类
作用：封装了pthread库中的互斥锁功能，在创建时会自动加锁
*/
class Mutex: Nonecopyable {
public:
    typedef ScopedLocalImpl<Mutex> Lock;   // Lock是ScopedLocalImpl<Mutex>类的别名，局部互斥锁

    Mutex() {
        CondPanic(0 == pthread_mutex_init(&m_, nullptr), "lock init error");  // 默认属性初始化互斥锁m_
    }

    void lock() {
        CondPanic(0 == pthread_mutex_lock(&m_), "lock error");   // 加锁
    }

    void unlock() {
        CondPanic(0 == pthread_mutex_unlock(&m_), "unlock error");   // 解锁
    }

    ~Mutex() {
        CondPanic(0 == pthread_mutex_destroy(&m_), "destory lock error");   // 销毁锁
    }

private:
    pthread_mutex_t m_;     // pthread_mutex_t类型的互斥锁
};

/*
RWMutex：读写锁类
作用：封装pthread库中的读写锁功能
*/
class RWMutex: Nonecopyable {
public:
    typedef ReadScopedLockImpl<RWMutex> ReadLock;   // 局部读锁ReadLock
    typedef WriteScopedLockImpl<RWMutex> WriteLock;   // 局部写锁WriteLock

    RWMutex() {
        pthread_rwlock_init(&m_, nullptr);  // 默认属性初始化读写锁
    }

    void rdlock() {
        pthread_rwlock_rdlock(&m_);   // 加读锁
    }

    void wrlock() {
        pthread_rwlock_wrlock(&m_);   // 加写锁
    }

    void unlock() {
        pthread_rwlock_unlock(&m_);   // 解锁
    }

    ~RWMutex() {
        pthread_rwlock_destroy(&m_);    // 销毁读写锁
    }

private:
    pthread_rwlock_t m_;
};

}
#endif
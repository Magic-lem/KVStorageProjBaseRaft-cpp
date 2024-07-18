//
// 定时器类和定时器管理类的声明，管理定时器
// created by magic_pri on 2024-6-28
//
#ifndef __MONSOON_TIMER_H__
#define __MONSOON_TIMER_H__

#include <memory>
#include <functional>
#include <set>
#include "mutex.hpp"

namespace monsoon {

class TimerManager;  // 先声明一下管理类，以可以在Timer类中引为友元类

/*
Timer 定时器类
主要功能：代表一个定时器，负责在指定时间执行某个回调函数
*/
class Timer: public std::enable_shared_from_this<Timer> {

friend class TimerManager; // 友元类

public:
    typedef std::shared_ptr<Timer> ptr;

    bool cancel();   // 取消定时器
    bool refresh();   // 刷新定时器，重新开始计时
    bool reset(uint64_t ms, bool from_now);  // 重置定时器

private:
    // 将构造函数置为私有，确保只能由TimerManager来构建
    Timer(uint64_t ms, std::function<void()> cb, bool recuring, TimerManager *manager); 
    Timer(uint64_t ms);

    bool recurring_ = false;  // 是否是循环定时器
    uint64_t ms_ = 0;   // 定时器的间隔时间（执行周期）
    uint64_t next_ = 0;  // 精确的下次触发时间点
    std::function<void()> cb_;  // 回调函数
    TimerManager *manager_ = nullptr;  // 管理器

private:
    // 仿函数：两个Timer对象的比较方法（用于有序容器中的排序）
    struct Comparator {
        bool operator()(const Timer::ptr &lhs, const Timer::ptr &rhs) const;
    };
};

/*
TimerManager：定时器管理类
主要作用：管理多个定时器，提供添加、删除、获取定时器的功能
*/
class TimerManager {

friend class Timer;

public:
    TimerManager();
    ~TimerManager();

    // 添加定时器
    Timer::ptr addTimer(uint64_t ms, std::function<void()> cb, bool recuring = false);
    // 添加一个条件计时器，当weak_cond指向一个有效对象时才发挥作用
    Timer::ptr addConditionTimer(uint64_t ms, std::function<void()> cb, std::weak_ptr<void> weak_cond,
                                 bool recuriing = false);
    
    uint64_t getNextTimer();    // 到最近一个定时器触发时间的时间间隔（ms）
    void listExpiredCb(std::vector<std::function<void()>> &cbs);    // 获取所有已过期定时器的回调函数列表
    bool hasTimer();  // 是否有定时器

protected:
    // 当有新的定时器插入到定时器首部时，要执行的函数
    virtual void OnTimerInsertedAtFront() = 0;  // 纯虚函数，需要派生类实现
    // 将定时器添加到管理器中（在添加定时器函数中调用）
    void addTimer(Timer::ptr val, RWMutex::WriteLock &lock);

private:
    // 检测服务器时间是否被调整（时钟回绕，clock rollover）
    bool detectCLockRolllover(uint64_t now_ms);

    RWMutex mutex_;   // 读写锁对象
    std::set<Timer::ptr, Timer::Comparator> timers_;  // 定时器集合，有序集合，按照制定的规则排序
    bool tickled_ = false; // 是否触发nTimerInsertedAtFront
    uint64_t previouseTime_ = 0;    // 上次记录的时间，用于比较是否出现时空回绕
};

}
#endif
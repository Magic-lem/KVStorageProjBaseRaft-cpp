//
// 定时器类和定时器管理类的实现
// created by magic_pri on 2024-6-28
//

#include "timer.hpp"
#include "utils.hpp"

namespace monsoon {

/*
Timer中比较仿函数的实现
返回true代表lhs排在rhs之前，false则相反
*/
bool Timer::Comparator::operator()(const Timer::ptr &lhs, const Timer::ptr &rhs) const {
    if (!lhs && !rhs) return false; // 两个都为空

    if (!lhs) return true;   // lhs为空，rhs不为空

    if (!rhs) return false;  // lhs不为空，rhs为空

    // 谁的下一次触发先到，谁在前面
    if (lhs->next_ < rhs->next_) return true;
    if (lhs->next_ > rhs->next_) return false;

    // 如果两个同时触发，则根据指针地址排序，指针地址较小的排在前面
    return lhs.get() < rhs.get();
}


/*
Timer构造函数
主要作用：初始化Timer对象，包括定时器属性、触发周期
*/
Timer::Timer(uint64_t ms, std::function<void()> cb, bool recuring, TimerManager *manager):
    recurring_(recuring), ms_(ms), cb_(cb), manager_(manager) {
    next_ = GetElapsedMS() + ms_;
} 
Timer::Timer(uint64_t ms) : ms_(ms) {}

/*
Timer::cancel 取消定时器
主要作用：将定时器从集合中删除
*/
bool Timer::cancel() {
    RWMutex::WriteLock lock(manager_->mutex_);  // 加写锁，保护公共资源timers_
    if (cb_) {  // 存在回调函数
        cb_ = nullptr;  // 回调函数置为空
        auto it = manager_->timers_.find(shared_from_this());  // 从定时器集合中找到本定时器所在位置
        manager_->timers_.erase(it);
        return true;
    }
    return false;  // 不存在
}

/*
Timer::refresh 刷新定时器
主要作用：重置触发时间
*/
bool Timer::refresh() {
    RWMutex::WriteLock lock(manager_->mutex_);  // 写锁
    if (!cb_) return false;  // 没有绑定函数，不需要刷新

    // 从定时器集合中找到本定时器
    auto it = manager_->timers_.find(shared_from_this());
    if (it == manager_->timers_.end()) return false;  // 没找到

    manager_->timers_.erase(it);  // 删除当前的定时器
    next_ = GetElapsedMS() + ms_; // 重置触发时间
    manager_->timers_.insert(shared_from_this());   // 重新加到定时器集合中
    return true;
}

/*
Timer::reset 重置定时器
主要作用：重新设置触发周期
args: from_now = true   下次触发时间从当前时刻开始计算
      from_now = false  下次触发时间从上一次触发时间开始计算 
*/
bool Timer::reset(uint64_t ms, bool from_now) {
    if (ms == ms_ && !from_now) return true;  // 不需要修改

    RWMutex::WriteLock lock(manager_->mutex_);  // 加写锁
    
    if (!cb_) {     // 没有绑定函数
        return true;  
    }

    auto it = manager_->timers_.find(shared_from_this());
    if (it == manager_->timers_.end()) {
        return false;  // 没有找到
    }

    // 删除当前的定时器
    manager_->timers_.erase(it);

    // 添加重置后的定时器
    uint64_t start = 0;
    if (from_now) {     // 从当前时刻开始
        start = GetElapsedMS();
    } else {
        start = next_ - ms_;    // 从上一次触发开始
    }
    ms_ = ms;
    next_ = start + ms_;
    manager_->addTimer(shared_from_this(), lock);
    return true;
}


/*
TimerManager 构造函数
*/
TimerManager::TimerManager() {
    previouseTime_ = GetElapsedMS();  // 记录时间
}

/*
TimerManager 析构函数
*/
TimerManager::~TimerManager() {}

/*
Timer::ptr TimerManager::addTimer 
主要功能：新增一个定时器，将其加入到定时器集合中，并返回指向其的指针
*/
Timer::ptr TimerManager::addTimer(uint64_t ms, std::function<void()> cb, bool recurring) {
    Timer::ptr timer(new Timer(ms, cb, recurring, this));   // 新创建一个计时器
    RWMutex::WriteLock lock(mutex_);  // 加写锁
    addTimer(timer, lock);    // 同时将定时器添加到管理器的定时器集合中
    return timer;
}

/*
Timer::ptr TimerManager::addCOnditionTimer 创建一个条件定时器，并加入到管理器定时器集合中
主要功能：条件定时器是在每次触发后检查一个条件（这里通过std::weak_ptr实现），如果条件满足再执行回调函数
*/

// 辅助函数OnTimer：在条件满足时执行一个回调函数
static void OnTimer(std::weak_ptr<void> weak_cond, std::function<void()> cb) {
    std::shared_ptr<void> tmp = weak_cond.lock();   // 锁定弱指针，如果指向对象存在，则lock()会返回一个指向该对象的共享指针
    if (tmp) {  // 对象存在（条件满足），则执行回调函数
        cb();
    }
}
// 由于条件对象可能会含有指向timer的共享指针，所以这里用weak_ptr，防止循环引用
Timer::ptr TimerManager::addConditionTimer(uint64_t ms, std::function<void()> cb, std::weak_ptr<void> weak_cond,
                                            bool recuriing) {
    return addTimer(ms, std::bind(&OnTimer, weak_cond, cb), recuriing);  // 调用Timer::ptr addTimer函数，使用std::bind绑定可调用对象OnTimer和参数作为回调函数
}

/*
TimerManager::getNextTimer 函数
主要功能：获得到下一次触发的时间
*/
uint64_t TimerManager::getNextTimer() {
    RWMutex::ReadLock lock(mutex_);  // 加读锁
    tickled_ = false;   
    if (timers_.empty()){   // 没有定时器
        return ~0ull;  // 返回uint64_t的最大值
    }

    const Timer::ptr &next = *timers_.begin();  // next为Timer指针的别名
    uint64_t now_ms = GetElapsedMS();
    if (now_ms >= next->next_){ // 已经触发了
        return 0;
    } else {
        return next->next_ - now_ms;    // 返回还有多长时间
    }
}

/*
TimerManager::listExpiredCb 函数
主要功能：检查当前时间，获取所有已过期定时器的回调函数列表，如果是循环定时器，将其重新加入到管理器中
*/
void TimerManager::listExpiredCb(std::vector<std::function<void()>> &cbs) {
    uint64_t now_ms = GetElapsedMS();   // 获取当前时间
    std::vector<Timer::ptr> expired;  // 存储过期的定时器

    { // 如果不存在定时器，那么没有必要加写锁，直接返回了
        RWMutex::ReadLock lock(mutex_);  // 加读锁
        if (timers_.empty()) return;  // 第没有定时器
    }
    
    // 存在定时器，加写锁
    RWMutex::WriteLock lock(mutex_);
    if (timers_.empty()) return;  // 再检查一边，防止这期间被修改

    // 检测时钟回滚
    bool rollover = false;
    if (detectCLockRolllover(now_ms)) {
        rollover = true;  // 发生了时钟回滚
    }

    // 检查是否存在到期定时器
    if (!rollover && ((*timers_.begin())->next_ > now_ms)) {    // 如果没发生时钟回滚，且定时器集合中的定时器都没触发，则说明没有定时器过期
        return;  // 直接返回
    }

    // 否则说明有到期定时器
    Timer::ptr now_timer(new Timer(now_ms));   // 以当前时间创立一个定时器
    // 找到第一个未到期的定时器： 1. 如果发生了时钟回滚，那么全都是过期的；2. 如果没有，则通过lower_bound找到第一个不小于当前定时器的定时器
    auto it = rollover ? timers_.end() : timers_.lower_bound(now_timer);

    // 继续找第一个比新创建的定时器大的定时器
    while (it != timers_.end() && (*it)->next_ == now_ms) {
        ++it;
    }
    // 此时it在第一个还没到期的定时器上，在it之前的均是过期的
    expired.insert(expired.begin(), timers_.begin(), it);  // 将过期的定时器批量插入
    timers_.erase(timers_.begin(), it);    // 删除过期的定时器

    cbs.reserve(expired.size());   // 预分配内存
    for (auto &timer: expired) {
        cbs.push_back(timer->cb_);
        // 如果是循环触发的定时器，再将其加入到定时器集合中
        if (timer->recurring_) {
            timer->next_ = now_ms + timer->ms_;
            timers_.insert(timer);
        } else {
            timer->cb_ = nullptr;  // 否则，将定时器的回调函数置为空
        }
    }
}

/*
void TimerManager::addTimer 函数
主要作用：用于将新创建好的定时器加入到定时器集合中
*/
void TimerManager::addTimer(Timer::ptr val, RWMutex::WriteLock &lock) {
    auto it = timers_.insert(val).first;    // 插入val，并获得插入地址
    
    // 判断是否是添加到了集合的最前面，为最早触发
    bool at_front = (it == timers_.begin()) && !tickled_;
    if (at_front) {
        tickled_ = true;
    }
    lock.unlock();  // 解除写锁，允许其他线程访问timers_

    // 如果本定时器插入到了最前面，需要进行额外处理
    if (at_front) {
        OnTimerInsertedAtFront();
    }
}

/*
bool TimerManager::detectClockRolllover 
主要作用：检查是否发生了时钟回绕
*/
bool TimerManager::detectCLockRolllover(uint64_t now_ms) {
    bool rollover = false;
    if (now_ms < previouseTime_ && now_ms < (previouseTime_ - 60 * 60 * 1000)) {    // 比上一次执行的时间还小一个小时以上，则判断发生了时钟回绕
        rollover = true;
    }
    previouseTime_ = now_ms;
    return rollover;
}

/*
bool TimerManager::hasTimer
主要作用：定时器集合中是否有定时器
*/
bool TimerManager::hasTimer() {
    RWMutex::ReadLock lock(mutex_);  // 加读锁
    return timers_.empty();
}


}
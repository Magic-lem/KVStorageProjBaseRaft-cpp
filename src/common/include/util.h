// 
// 一些util方法的声明，子模块共用
// created by magic_pri on 2024-6-20
//

#ifndef UTIL_H
#define UTIL_H


// 格式化日至输出函数，能够打印详细的调试信息
void DPrintf(const char* format, ...);

// 定义一个线程安全的队列，用于异步写日志
template <typename T>
class LockQueue {
public:
  // 向队列中添加元素，写日志
  void Push(const T& data) {
    std::lock_guard<std::mutex> lock(m_mutex);   // 使用 lock_guard 锁定互斥锁，利用RAII的思想保证锁正确释放
    m_queue.push(data);   // 添加元素
    m_condvariable.notify_one();   // 唤醒一个因为锁而等待的线程，避免不必要的等待
  }

  // 取出队列元素，读日志
  T Pop() {
    std::unique_lock<std::mutex> lock(m_mutex);
    while (m_queue.empty()) {
      // 日志队列是空的，则进入等待，等有元素写入后被唤醒读取(通过条件变量)
      m_condvariable.wait(lock);
    }
    // 取出最前面的元素
    T data = m_queue.front();
    m_queue.pop();
    return data;
  }

  // 带有超时时间的取出队列元素，如果在指定时间内队列为空，没能取出，则返回 false
  bool timeOutPop(int timeout, T* ResData) {
    std::unique_lock<std::mutex> lock(m_mutex);
    
    auto now = std::chrono::system_clock::now();    // 获取当前时间
    auto timeout_time = now + std::chrono::milliseconds(timeout);   // 计算超时时间

    while (m_queue.empty()) {   
      if (m_condvariable.wait_until(lock, timeout_time) == std::cv_status::timeout) { // 如果队列为空，且以及超时了，则返回失败
        return false;
      } else {    // 否则不断循环，直到队列不为空或超时
        continue;
      }
    }

    T data = m_queue.front();
    m_queue.pop();
    *ResData = data;
    return true;
  }

private:
  std::queue<T> m_queue;    // 存储队列元素的容器
  std::mutex m_mutex;     // 互斥锁，用于保护队列的访问
  std::condition_variable m_condvariable;     // 条件变量，用于线程间的同步
}
// 在LockQueue中，两个对锁的管理用到了RAII的思想，防止中途出现问题而导致资源无法释放的问题！！！
// std::lock_guard 和 std::unique_lock 都是 C++11 中用来管理互斥锁的工具类，它们都封装了 RAII（Resource Acquisition Is
// Initialization）技术，使得互斥锁在需要时自动加锁，在不需要时自动解锁，从而避免了很多手动加锁和解锁的繁琐操作。
// std::lock_guard 是一个模板类，它的模板参数是一个互斥量类型。当创建一个 std::lock_guard
// 对象时，它会自动地对传入的互斥量进行加锁操作，并在该对象被销毁时对互斥量进行自动解锁操作。std::lock_guard
// 不能手动释放锁，因为其所提供的锁的生命周期与其绑定对象的生命周期一致。 std::unique_lock
// 也是一个模板类，同样的，其模板参数也是互斥量类型。不同的是，std::unique_lock 提供了更灵活的锁管理功能。可以通过
// lock()、unlock()、try_lock() 等方法手动控制锁的状态。当然，std::unique_lock 也支持 RAII
// 技术，即在对象被销毁时会自动解锁。另外， std::unique_lock 还支持超时等待和可中断等待的操作。



#endif
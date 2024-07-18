// 
// 一些util方法的声明，子模块共用
// created by magic_pri on 2024-6-20
//

#ifndef UTIL_H
#define UTIL_H

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/serialization/access.hpp>
#include <condition_variable>  // pthread_condition_t
#include <functional>
#include <iostream>
#include <mutex>  // pthread_mutex_t
#include <queue>
#include <random>
#include <sstream>
#include <thread>
#include "config.h"

/*
  格式化日至输出函数，能够打印详细的调试信息
*/
void DPrintf(const char* format, ...);


/*
  定义一个线程安全的队列，用于异步写日志
*/
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
      // m_condvariable.wait_until会在等待期间让出锁陷入阻塞，直到收到锁的通知或这超时
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
};
// 在LockQueue中，两个对锁的管理用到了RAII的思想，防止中途出现问题而导致资源无法释放的问题！！！
// std::lock_guard 和 std::unique_lock 都是 C++11 中用来管理互斥锁的工具类，它们都封装了 RAII（Resource Acquisition Is
// Initialization）技术，使得互斥锁在需要时自动加锁，在不需要时自动解锁，从而避免了很多手动加锁和解锁的繁琐操作。
// std::lock_guard 是一个模板类，它的模板参数是一个互斥量类型。当创建一个 std::lock_guard
// 对象时，它会自动地对传入的互斥量进行加锁操作，并在该对象被销毁时对互斥量进行自动解锁操作。std::lock_guard
// 不能手动释放锁，因为其所提供的锁的生命周期与其绑定对象的生命周期一致。 std::unique_lock
// 也是一个模板类，同样的，其模板参数也是互斥量类型。不同的是，std::unique_lock 提供了更灵活的锁管理功能。可以通过
// lock()、unlock()、try_lock() 等方法手动控制锁的状态。当然，std::unique_lock 也支持 RAII
// 技术，即在对象被销毁时会自动解锁。另外， std::unique_lock 还支持超时等待和可中断等待的操作。



/*
  DEFER宏：一种在作用域结束时自动执行某些操作的方法，常用于确保资源释放、文件关闭、解锁互斥量等操作，即便函数因异常或其他原因提前返回也能确保这些操作被执行。
*/
template <class F>
class DeferClass {
public:
  DeferClass(F&& f) : m_func(std::forward<F>(f)) {}     // 万能引用构造函数
  DeferClass(const F& f) : m_func(*f) {}    // 常量左值引用构造函数
  ~DeferClass() { m_func(); }   // 析构函数，当对象销毁时，去调用m_func函数

  // 拷贝构造函数和赋值运算符被删除，以防止对象被复制
  DeferClass(const DeferClass& e) = delete;
  DeferClass& operator=(const DeferClass& e) = delete;

private:
  F m_func;   // 需要自动执行的函数
};

/*-------------------------------定义DEFER宏------------------------------------------------------*/
#define _CONCAT(a, b) a##b    // _CONCAT将两个标识符拼接在一起，生成一个新的标识符
// _MAKE_DEFER_(line)：定义一个DeferClass对象，名称为defer_placeholder和line拼接，传入一个空lambda表达式
#define _MAKE_DEFER_(line) DeferClass _CONCAT(defer_placeholder, line) = [&]()      

#undef DEFER
#define DEFER _MAKE_DEFER_(__LINE__)   // DEFER，将当前行号传递给 _MAKE_DEFER_，从而生成唯一的变量名称
// 使用示例
/**
void someFunction() {
  std::ofstream file("example.txt");

  DEFER {
    file.close();
  };

  // ... 其他操作 ...
}
DEFER 宏创建了一个 DeferClass 对象，命名为 defer_placeholder 加上当前行号，例如 defer_placeholder123。
这个对象在 DEFER 宏所在的作用域结束时（即 someFunction 返回或异常抛出时）自动调用 file.close()。
 */
/*-------------------------------定义DEFER宏------------------------------------------------------*/


/*
  自实现的断言
*/
void myAssert(bool condition, std::string message = "Assertion failed!");


std::chrono::_V2::system_clock::time_point now();


/*
  sleepNMilliseconds：睡眠一段时间
*/
void sleepNMilliseconds(int N);



/*
  Op是kv传递给raft的command
  Op表示客户端操作请求，并提供将操作序列化为字符串的方法
*/
class Op {
public:
  std::string Operation;  // "Get" "Put" "Append" 等作用于KV数据库的操作
  std::string Key;        // key
  std::string Value;      // value
  std::string ClientId;  //客户端号码
  int RequestId;         //客户端号码请求的Request的序列号，为了保证线性一致性

public:
  /*
  asString：将 Op 对象序列化为字符串
  // todo
  // 为了协调raftRPC中的command只设置成了string,这个的限制就是正常字符中不能包含|
  // 当然后期可以换成更高级的序列化方法，比如protobuf
  */
  std::string asString() const {
    std::stringstream ss;
    boost::archive::text_oarchive oa(ss);   // 使用boost库序列化

    // write class instance to archive
    oa << *this;
    // close archive

    return ss.str();
  }

  /*
  parseFromString(std::string str) 方法:将字符串反序列化解析为 Op 对象
  */
  bool parseFromString(std::string str) {
    std::stringstream iss(str);
    boost::archive::text_iarchive ia(iss);
    // read class state from archive
    ia >> *this;
    return true;  // todo : 解析失敗如何處理，要看一下boost庫了
  }

public: 
  /*
  重载opeartor<<操作符，以自定义格式输出 Op 对象。
  输出包括 Operation、Key、Value、ClientId 和 RequestId 字段。
  */
  friend std::ostream& operator<<(std::ostream& os, const Op& obj) {
    os << "[MyClass:Operation{" + obj.Operation + "},Key{" + obj.Key + "},Value{" + obj.Value + "},ClientId{" +
              obj.ClientId + "},RequestId{" + std::to_string(obj.RequestId) + "}";
    return os;
  }

private:
  // 集成boost库，利用boost::serialization 库进行序列化和反序列化。
  friend class boost::serialization::access;   
  template <class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    ar& Operation;
    ar& Key;
    ar& Value;
    ar& ClientId;
    ar& RequestId;
  }
};



///////////////////////////////////////////////kvserver reply err to clerk

const std::string OK = "OK";
const std::string ErrNoKey = "ErrNoKey";
const std::string ErrWrongLeader = "ErrWrongLeader";

////////////////////////////////////获取可用端口

bool isReleasePort(unsigned short usPort);

bool getReleasePort(short& port);


template <typename... Args>
std::string format(const char* format_str, Args... args) {
  std::stringstream ss;
  int _[] = {((ss << args), 0)...};
  (void)_;
  return ss.str();
}

std::chrono::milliseconds getRandomizedElectionTimeout();

#endif
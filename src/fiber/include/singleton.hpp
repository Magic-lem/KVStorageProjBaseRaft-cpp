//
// 单例模式封装类
// created by magic_pri on 2024-6-27
//
#ifndef __MONSOON_SINGLETON_H__
#define __MONSOON_SINGLETON_H__

#include <memory>

namespace monsoon {

// namespace { // 匿名命名空间，只在本文件中可见
// /*
// 辅助模板函数，用于生成单例的实例
// GetInstanceX：返回类型T的静态局部变量的引用
// GetInstancePtr：返回一个指向类型T的静态局部变量的共享指针
// */
// template<class T, class X, int N>
// T &GetInstanceX() {
//     static T v;
//     return v;
// }

// template<class T, class X, int N>
// std::shared_ptr<T> GetInstancePtr() {
//     static std::shared_ptr<T> v(new T);
//     return v;
// }
// }

/**
 * @brief 单例模式封装类
 * @details T 类型
 *          X 为了创造多个实例对应的Tag
 *          N 同一个Tag创造多个实例索引
 */
template<class T, class X = void, int N = 0>
class Singleton {
public:
    // 返回单例裸指针
    static T *GetInstance() {
        static T v;
        return &v;
    }
};

/**
 * @brief 单例模式智能指针封装类
 * @details T 类型
 *          X 为了创造多个实例对应的Tag
 *          N 同一个Tag创造多个实例索引
 */
template<class T, class X = void, int N = 0>
class SingletonPtr {
public:
    // 返回单例智能指针
    static std::shared_ptr<T> GetInstance() {
        static std::shared_ptr<T> v(new T);
        return v;
    }
};

}


#endif

// 
// 禁用拷贝构造和赋值的基类
// created by magic_pri on 2024-6-24
//

#ifndef __SYLAY_NONCOPYABLE_H__
#define __SYLAY_NONCOPYABLE_H__

namespace monsoon {
/*
Nonecopyable：作为基类，使派生类中禁用拷贝构造和赋值
主要作用：确保派生类对象无法通过拷贝构造函数或赋值运算符创建新的实例对象
*/
class Nonecopyable {
public:
    Nonecopyable() = default;
    ~Nonecopyable() = default;
    Nonecopyable(const Nonecopyable &) = delete;
    Nonecopyable operator=(const Nonecopyable) = delete;
};
}

#endif
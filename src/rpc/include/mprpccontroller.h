//
// RPC控制器类，用于控制ROC方法的执行过程并管理其状态和错误信息
// created by magic_pri on 2024-6-21
//

#ifndef MPRPCCONTROLL:ER_H
#define MPRPCCONTROLLER_H

#include <google/protobuf/service.h>
#include <string>

/*
继承自'google::protobuf::RpcController'，提供了一些方法来处理RPC调用中的错误和状态，并且可以在需要时取消调用。
主要作用：
    1. 控制RPC调用的执行过程
    2. 错误处理
    3. 调用取消 (原代码未实现)

新增 by magic_pri:
    实现了取消调用系列功能
*/
class MprpcController: public google::protobuf::RpcController {
public:
    MprpcController();    // 构造函数

    void Reset() override;   // 重置控制器的状态和错误信息
    bool Failed() const override;   // 检查RPC调用是否失败
    std::string ErrorText() const override;  // 获得错误信息
    void SetFailed(const std::string& reason) override;   // 设为调用失败，记录错误信息

    // 取消调用系列功能
    void StartCancel() override;   // 启动取消调用
    bool IsCanceled() const override;   // 检查当前的RPC调用是否已经取消
    void NotifyOnCancel(google::protobuf::Closure* callback) override;  // RPC调用被取消时，会调用这个回调函数

private:
    bool m_failed;   // 表示RPC方法执行过程中的状态，是否失败
    std::string m_errText;  // 记录RPC方法执行过程中的错误信息
    bool m_canceled;  // 标识是否取消
    google::protobuf::Closure* m_cancelCallback;   // 取消时的回调函数
};


#endif
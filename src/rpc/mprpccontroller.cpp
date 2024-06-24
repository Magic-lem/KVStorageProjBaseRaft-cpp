//
// RPC控制器类的具体实现
// created by magic_pri on 2024-6-21
//

#include "./include/mprpccontroller.h"

// 构造函数，初始化两个成员变量
MprpcController::MprpcController(): m_failed(false), m_errText(""), m_canceled(false), m_cancelCallback(nullptr) {}

// 重置控制器的状态和错误信息
void MprpcController::Reset() {
    m_failed = false;
    m_errText = "";
    m_canceled = false;
    m_cancelCallback = nullptr;
}

// 检查RPC调用是否失败
bool MprpcController::Failed() const {
    return m_failed;
}

// 获得错误信息
std::string MprpcController::ErrorText() const {
    return m_errText;
}

// 将控制器设为调用失败的状态
void MprpcController::SetFailed(const std::string& reason) {
    m_failed = true;
    m_errText = reason;
}

// 启动取消调用
void MprpcController::StartCancel() {
    m_canceled = true;
    // 如果存在回调函数，则执行
    if (m_cancelCallback) m_cancelCallback->Run();
}

// 检查当前的RPC调用是否取消
bool MprpcController::IsCanceled() const {
    return m_canceled;
}

// 绑定回调函数
void MprpcController::NotifyOnCancel(google::protobuf::Closure* callback){
    m_cancelCallback = callback;

    if (m_canceled && m_cancelCallback) m_cancelCallback->Run();
}
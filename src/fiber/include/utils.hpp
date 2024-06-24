#ifndef __MONSOON_UTIL_H__
#define __MONSOON_UTIL_H__

#include <string>
#include <iostream>
#include <vector>
#include <sstream>      // stringstream
#include <stdio.h>     // sscanf()
#include <execinfo.h>   // ::backtrace()
#include <cxxabi.h>
#include <assert.h>

namespace monsoon {

/*
demangle
功能：将原始函数名解析为可读的函数名。（编译器生成的函数名会经过“修饰”或“混淆”，不易读）
Input：const char* str   原始函数名字符串
*/
static std::string demangle (const char *str) {
    size_t size = 0;   // 解析后函数名的大小
    int status = 0;   // 用于存储'abi::__cxa_demangle'函数的返回状态
    std::string rt;   // 存储解析过程中的中间结构
    rt.resize(256);

    // 从字符串中提取函数名部分
    if (1 == sscanf(str, "%*[^(]%*[^_]%255[^)+]", &rt[0])) {
        // 解析函数
        char *v = abi::__cxa_demangle(&rt[0], nullptr, &size, &status);
        if (v) { // 解析成功
            std::string result(v);
            free(v);   // 手动释放动态内存
            return result;
        }
    }

    // 解析失败，尝试再次提取最多255个字符作为函数名
    if (1 == sscanf(str, "%255s", &rt[0])) {
        return rt;
    }

    return str;   // 都失败，直接返回原始字符串
}


/*
Backtrace
功能：获取当前线程的调用栈信息，并将解析后的函数名存储在一个字符串向量中
Input：std::vector<std::string> &bt   用与存调用栈信息的字符串向量
       int size
       int skip
*/
static void Backtrace(std::vector<std::string> &bt, int size, int skip) {
    // 分配用于存储调用栈信息的数组
    void **array = (void **)malloc((sizeof(void *) * size));     // 根据size开辟内存，分配一个指针数组，用于存储调用栈的地址信息
    // 获取调用栈地址信息
    size_t s = ::backtrace(array, size);   // s为调用栈的地址条目个数

    // 获取调用栈符号信息
    char **strings = backtrace_symbols(array, s);  // 将调用栈地址信息转换为符号信息，存储在字符串数组中
    if (strings == NULL) {
        std::cout << "backtrace_symbols_error" << std::endl;
        return;
    }

    // 解析每一个调用栈的信息，并将解析后的函数名添加到bt中
    for (size_t i = skip; i < s; ++i) {
        bt.push_back(demangle(strings[i]));
    }

    free(strings);
    free(array);
}


/*
BacktraceToString
功能：获取调用栈信息，并将其转换为一个字符串，方便输出或日志记录
Input： int size  获取调用栈的最大深度
        int skip  指定跳过的调用栈层数（常用于跳过当前函数及其调用者）
        const std::string &prefix  在每一行调用栈信息前加入的前缀字符串
Output：std::string  一个包含调用栈信息的字符串，每一行表示一个调用栈条目，每行前带有指定的前缀
*/
static std::string BacktraceToString(int size, int skip, const std::string &prefix) {
    // 定义一个存储栈信息的容器，用于存储调用栈信息的各个条目
    std::vector<std::string> bt;
    Backtrace(bt, size, skip);   // 获取调用栈信息
    std::stringstream ss;   // 字符串流，将调用栈信息拼接成一个完整的字符串
    for (size_t i = 0; i < bt.size(); ++i) {
        ss << prefix << bt[i] << std::endl;     // 每一行前都有一个前缀字符策划prefix
    }

    return ss.str();   // 字符流中的完整字符串
}


/*
CondPanic  
功能：断言处理静态函数，检查一个布尔条件，若条件不满足，输出错误信息和调用栈，触发断言
Input：bool condition 要检查的布尔条件
       std::string err 条件不满足时的输出错误信息
*/
static void CondPanic(bool condition, std::string err){
    if (!condition) {  // 条件不满足
        // 输出错误信息
        std::cout << "[assert by] ( " << __FILE__ << ":" << __LINE__ << " ), err: " << err << std::endl;
        // 输出调用栈
        std::cout << "[backtrace]\n" << BacktraceToString(6, 3, "") << std::endl;
        assert(condition);
    }
}
}

#endif
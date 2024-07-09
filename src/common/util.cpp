//
// util方法的实现
// created by magic_pri on 2024-6-20
// 

#include "./include/util.h"
#include "./include/config.h"
#include <ctime>    // time_t   time()  tm  localtime()
#include <stdarg.h>   // va_list
#include <cstdio>  // printf()

/*
DPrintf()
用于格式化输出带有时间戳的日志信息
*/
void DPrintf(const char *format, ...) {
    if (Debug) {
        time_t now = time(nullptr);  // 当前时间，以秒为单位的时间戳
        tm *nowtm = localtime(&now);  // 将时间戳转换为本地时间，得到一个tm结构体

        // 处理可变参数
        va_list args;  // 定义一个'va_list'变量，用于访问可变参数
        va_start(args, format);  // 初始化args变量，使其指向可变参数列表的第一个参数

        // 输出时间戳
        std::printf("[%d-%d-%d-%d-%d-%d] ", nowtm->tm_year + 1900, nowtm->tm_mon + 1, nowtm->tm_mday, 
                    nowtm->tm_hour, nowtm->tm_min, nowtm->tm_sec);   // 打印时间戳，由于tm_year是从1900开始，需要+1900。tm_mon从0开始，需要+1    }

        // 输出日志信息
        std::vprintf(format, args);    // 根据传入的格式化字符串和参数列表打印日志信息
        std::printf("\n");

        va_end(args);    // 结束处理可变参数
    }
}


/*
myAssert
用于断言
*/
void myAssert(bool condition, std::string message) {
  if (!condition) {
    std::cerr << "Error: " << message << std::endl;
    std::exit(EXIT_FAILURE);
  }
}
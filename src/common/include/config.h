// 
// 一些配置信息
// created by magic_pri on 2024-6-20
//

#ifndef CONFIG_H
#define CONFIG_H

const bool Debug = true;    // 是否为Debug模式

const int debugMul = 1;  // 时间单位：time.Millisecond，不同网络环境rpc速度不同，因此需要乘以一个系数
const int HeartBeatTimeout = 25 * debugMul;  // 心跳时间一般要比选举超时小一个数量级
const int ApplyInterval = 10 * debugMul;     // 将消息应用到状态机上的时间间隔

const int minRandomizedElectionTime = 300 * debugMul;  // ms
const int maxRandomizedElectionTime = 500 * debugMul;  // ms

// 协程相关设置
const int FIBER_THREAD_NUM = 1;     // 线程池大小
const bool FIBER_USE_CALLER_THREAD = false; // 是否use_caller模式
#endif
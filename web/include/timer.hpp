#ifndef TIMER_HPP
#define TIMER_HPP

#include <functional>
#include <chrono>
#include <atomic>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <thread>
#include <iostream>

using namespace std::chrono_literals;

// 定时器轮类
class TimerWheel {
public:
    using TimeoutCallback = std::function<void()>;  // 超时回调类型
    
    // 构造函数
    explicit TimerWheel(size_t slots = 60, 
                      std::chrono::milliseconds interval = 10ms);
    ~TimerWheel();
    
    void Start();  // 启动定时器
    void Stop();   // 停止定时器
    uint64_t AddTimeout(std::chrono::milliseconds timeout, TimeoutCallback cb);  // 添加超时任务
    void CancelTimeout(uint64_t id);  // 取消超时任务
    
    // 获取信息方法
    size_t GetCurrentSlot() const;  // 获取当前槽位
    size_t GetWheelSize() const;    // 获取轮大小
    bool IsRunning() const;         // 是否运行中
    size_t CountTasks() const;      // 任务计数
    void PrintDebugInfo() const;    // 打印调试信息

private:
    // 定时任务结构
    struct TimerTask {
        uint64_t id;            // 任务ID
        TimeoutCallback callback;  // 回调函数
    };
    
    void RunLoop();  // 运行循环
    
    std::vector<std::unordered_map<uint64_t, TimerTask>> wheel_;  // 定时器轮
    size_t slots_;               // 槽位数
    std::chrono::milliseconds interval_;  // 间隔时间
    size_t current_slot_;        // 当前槽位
    mutable std::mutex mutex_;   // 互斥锁
    std::atomic<uint64_t> next_id_;  // 下一个ID
    std::atomic<bool> running_;  // 运行标志
    std::thread worker_thread_;  // 工作线程
};

#endif
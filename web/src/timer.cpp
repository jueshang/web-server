#include "timer.hpp"
#include <algorithm>

// 构造函数
TimerWheel::TimerWheel(size_t slots, std::chrono::milliseconds interval)
    : slots_(slots),
      interval_(interval),
      current_slot_(0),
      next_id_(0),
      running_(false) {
    if (slots == 0) {
        throw std::invalid_argument("Timer wheel slots cannot be zero");
    }
    if (interval <= 0ms) {
        throw std::invalid_argument("Timer interval must be positive");
    }
    wheel_.resize(slots_);  // 初始化轮
}

// 析构函数
TimerWheel::~TimerWheel() {
    Stop();  // 停止定时器
    if (worker_thread_.joinable()) {
        worker_thread_.join();  // 等待工作线程结束
    }
}

// 启动定时器
void TimerWheel::Start() {
    if (!running_.exchange(true)) {
        worker_thread_ = std::thread(&TimerWheel::RunLoop, this);  // 创建工作线程
    }
}

// 停止定时器
void TimerWheel::Stop() {
    running_ = false;
}

// 运行循环
void TimerWheel::RunLoop() {
    using clock = std::chrono::steady_clock;
    auto next_tick = clock::now() + interval_;  // 计算下一个tick时间
    
    while (running_) {
        std::vector<TimerTask> tasks_to_run;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto& tasks = wheel_[current_slot_];  // 获取当前槽位的任务
            
            // 移动所有任务到执行列表
            for (auto it = tasks.begin(); it != tasks.end();) {
                tasks_to_run.push_back(std::move(it->second));
                it = tasks.erase(it);
            }
            
            current_slot_ = (current_slot_ + 1) % slots_;  // 移动到下一个槽位
        }
        
        // 执行所有到期任务
        for (auto& task : tasks_to_run) {
            try {
                task.callback();  // 执行回调
            } catch (...) {}  // 忽略异常
        }
        
        // 等待到下一个tick
        std::this_thread::sleep_until(next_tick);
        next_tick += interval_;
    }
}

// 添加超时任务
uint64_t TimerWheel::AddTimeout(std::chrono::milliseconds timeout, TimeoutCallback cb) {
    if (timeout <= 0ms) {
        cb();  // 立即执行
        return ++next_id_;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t id = ++next_id_;
    
    // 计算需要的ticks和目标槽位
    size_t ticks = (timeout + interval_ - 1ms) / interval_;
    size_t target_slot = (current_slot_ + ticks) % slots_;
    
    // 添加到目标槽位
    wheel_[target_slot].emplace(id, TimerTask{id, std::move(cb)});
    return id;
}

// 取消超时任务
void TimerWheel::CancelTimeout(uint64_t id) {
    if (id == 0) return;
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& slot : wheel_) {
        if (slot.erase(id) > 0) break;  // 找到并删除任务
    }
}

// 获取当前槽位
size_t TimerWheel::GetCurrentSlot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_slot_;
}

// 获取轮大小
size_t TimerWheel::GetWheelSize() const {
    return slots_;
}

// 检查是否运行中
bool TimerWheel::IsRunning() const {
    return running_;
}

// 统计任务数
size_t TimerWheel::CountTasks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t count = 0;
    for (const auto& slot : wheel_) {
        count += slot.size();  // 累加每个槽位的任务数
    }
    return count;
}

// 打印调试信息
void TimerWheel::PrintDebugInfo() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "=== Timer Debug ==="
              << "\nCurrent slot: " << current_slot_
              << "\nRunning: " << running_
              << "\nTotal tasks: " << CountTasks()
              << "\nTasks per slot:";
    for (size_t i = 0; i < wheel_.size(); ++i) {
        if (!wheel_[i].empty()) {
            std::cout << "\n  Slot " << i << ": " << wheel_[i].size() << " tasks";
        }
    }
    std::cout << std::endl;
}
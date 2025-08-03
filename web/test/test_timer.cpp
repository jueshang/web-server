#include "timer.hpp"
#include <iostream>
#include <atomic>
#include <chrono>
#include <cassert>

void TestSingleTimer() {
    std::cout << "\n=== Test 1: Single Timer ===" << std::endl;
    TimerWheel timer(60, 10ms);
    timer.Start();
    
    std::atomic<bool> triggered(false);
    auto start = std::chrono::steady_clock::now();
    
    timer.AddTimeout(300ms, [&]() {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        std::cout << "Timer triggered after " << elapsed.count() << "ms" << std::endl;
        triggered = true;
    });
    
    auto wait_time = 450ms;
    while (!triggered && 
          std::chrono::steady_clock::now() - start < wait_time) {
        std::this_thread::sleep_for(10ms);
    }
    
    if (!triggered) {
        std::cerr << "ERROR: Timer not triggered!" << std::endl;
        timer.PrintDebugInfo();
        assert(false);
    }
    
    timer.Stop();
    std::cout << "Test passed!\n";
}

void TestMultipleTimers() {
    std::cout << "\n=== Test 2: Multiple Timers ===" << std::endl;
    TimerWheel timer(60, 10ms);
    timer.Start();
    
    const int test_count = 5;
    std::atomic<int> count(0);
    
    for (int i = 0; i < test_count; ++i) {
        auto timeout = (i+1)*100ms;
        timer.AddTimeout(timeout, [&, timeout]() {
            std::cout << timeout.count() << "ms timer triggered" << std::endl;
            count++;
        });
    }
    
    std::this_thread::sleep_for(600ms);
    timer.Stop();
    
    if (count != test_count) {
        std::cerr << "ERROR: Only " << count << "/" << test_count 
                 << " timers triggered!" << std::endl;
        assert(false);
    }
    std::cout << "Test passed!\n";
}

void TestTimerCancellation() {
    std::cout << "\n=== Test 3: Timer Cancellation ===" << std::endl;
    TimerWheel timer(60, 10ms);
    timer.Start();
    
    std::atomic<bool> should_not_trigger(false);
    auto id = timer.AddTimeout(200ms, [&]() {
        should_not_trigger = true;
    });
    
    std::this_thread::sleep_for(100ms);
    timer.CancelTimeout(id);
    
    std::this_thread::sleep_for(200ms);
    timer.Stop();
    
    if (should_not_trigger) {
        std::cerr << "ERROR: Cancelled timer was triggered!" << std::endl;
        assert(false);
    }
    std::cout << "Test passed!\n";
}

int main() {
    try {
        std::cout << "=== TimerWheel Test Suite ===" << std::endl;
        
        TestSingleTimer();
        TestMultipleTimers();
        TestTimerCancellation();
        
        std::cout << "\n=== All tests passed ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
}
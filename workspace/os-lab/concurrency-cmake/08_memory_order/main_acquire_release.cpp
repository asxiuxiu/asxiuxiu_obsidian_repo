// 实验08a：验证 Release-Acquire 的可见性
// 对应笔记：内存序——编译器和CPU如何"欺骗"你

#include <atomic>
#include <thread>
#include <cassert>
#include <iostream>

std::atomic<int> data{0};
std::atomic<bool> ready{false};

void producer() {
    data.store(42, std::memory_order_relaxed);
    ready.store(true, std::memory_order_release);
}

void consumer() {
    while (!ready.load(std::memory_order_acquire)) {}
    int val = data.load(std::memory_order_relaxed);
    assert(val == 42);
    std::cout << "Consumer saw data=" << val << " (assert passed!)\n";
}

int main() {
    std::cout << "=== Release-Acquire 可见性验证 ===\n";
    for (int i = 0; i < 1000; ++i) {
        data.store(0, std::memory_order_relaxed);
        ready.store(false, std::memory_order_relaxed);
        
        std::thread t1(producer);
        std::thread t2(consumer);
        t1.join(); t2.join();
    }
    std::cout << "1000 次测试全部通过！\n";
    return 0;
}

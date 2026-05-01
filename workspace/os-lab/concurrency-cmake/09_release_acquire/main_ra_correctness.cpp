// 实验09a：Release-Acquire 正确性验证
// 对应笔记：Release-Acquire——最实用的跨线程同步

#include <atomic>
#include <thread>
#include <iostream>

constexpr int ITERATIONS = 100'000;

std::atomic<int> data{0};
std::atomic<bool> ready{false};

void producer() {
    for (int i = 0; i < ITERATIONS; ++i) {
        data.store(i, std::memory_order_relaxed);
        ready.store(true, std::memory_order_release);
        while (ready.load(std::memory_order_relaxed)) {}
    }
}

void consumer() {
    for (int i = 0; i < ITERATIONS; ++i) {
        while (!ready.load(std::memory_order_acquire)) {}
        int val = data.load(std::memory_order_relaxed);
        if (val != i) {
            std::cout << "ERROR at iteration " << i
                      << ": expected " << i << " got " << val << "\n";
            return;
        }
        ready.store(false, std::memory_order_relaxed);
    }
    std::cout << "All " << ITERATIONS << " iterations passed!\n";
}

int main() {
    std::thread p(producer);
    std::thread c(consumer);
    p.join(); c.join();
    return 0;
}

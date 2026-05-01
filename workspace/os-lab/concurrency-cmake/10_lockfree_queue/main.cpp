// 实验10：SPSC 无锁队列 vs Mutex 队列性能对比
// 对应笔记：无锁队列——综合运用

#include "spsc_queue.hpp"
#include <queue>
#include <mutex>
#include <thread>
#include <chrono>
#include <iostream>

constexpr int COUNT = 5'000'000;

void bench_spsc() {
    SPSCQueue<int> q(1024);
    auto t1 = std::chrono::high_resolution_clock::now();
    
    std::thread producer([&]() {
        for (int i = 0; i < COUNT; ++i) {
            while (!q.enqueue(i)) {
                std::this_thread::yield();
            }
        }
    });
    
    std::thread consumer([&]() {
        int v, received = 0;
        while (received < COUNT) {
            if (q.dequeue(v)) ++received;
        }
    });
    
    producer.join(); consumer.join();
    auto t2 = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
    std::cout << "SPSC:    " << ms << " ms\n";
}

void bench_mutex() {
    std::queue<int> q;
    std::mutex mtx;
    auto t1 = std::chrono::high_resolution_clock::now();
    
    std::thread producer([&]() {
        for (int i = 0; i < COUNT; ++i) {
            std::lock_guard lk(mtx);
            q.push(i);
        }
    });
    
    std::thread consumer([&]() {
        int received = 0;
        while (received < COUNT) {
            std::lock_guard lk(mtx);
            if (!q.empty()) { q.pop(); ++received; }
        }
    });
    
    producer.join(); consumer.join();
    auto t2 = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
    std::cout << "Mutex:   " << ms << " ms\n";
}

int main() {
    std::cout << "=== SPSC 无锁队列 vs Mutex 队列 ===\n";
    std::cout << "Count: " << COUNT << "\n\n";
    bench_spsc();
    bench_mutex();
    return 0;
}

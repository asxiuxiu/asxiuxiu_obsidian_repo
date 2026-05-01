// 实验03：数据竞争演示
// 对应笔记：数据竞争——多线程的第一个敌人

#include <thread>
#include <iostream>
#include <atomic>
#include <chrono>

int bad_counter = 0;
std::atomic<bool> go{false};

void increment_bad() {
    for (int i = 0; i < 1000000; ++i) {
        ++bad_counter;
    }
}

void worker_manual_race() {
    while (!go.load()) {}
    int local = bad_counter;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    local = local + 1;
    bad_counter = local;
}

int main() {
    std::cout << "=== Part 1: 两个线程同时 ++counter ===\n";
    bad_counter = 0;
    {
        std::thread t1(increment_bad);
        std::thread t2(increment_bad);
        t1.join(); t2.join();
        std::cout << "counter = " << bad_counter
                  << " (expected 2000000, actual likely 1300000~1800000)\n\n";
    }

    std::cout << "=== Part 2: 手动放大竞争（强制交错） ===\n";
    bad_counter = 0;
    {
        std::thread t1(worker_manual_race);
        std::thread t2(worker_manual_race);
        go.store(true);
        t1.join(); t2.join();
        std::cout << "counter = " << bad_counter
                  << " (expected 2, actual likely 1)\n";
    }

    return 0;
}

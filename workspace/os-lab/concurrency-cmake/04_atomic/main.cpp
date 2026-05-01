// 实验04：原子操作
// 对应笔记：std::atomic——让操作不可分割

#include <atomic>
#include <thread>
#include <iostream>

std::atomic<int> good_counter{0};
int bad_counter = 0;

void increment_good() {
    for (int i = 0; i < 1'000'000; ++i) {
        ++good_counter;
    }
}

void increment_bad() {
    for (int i = 0; i < 1'000'000; ++i) {
        ++bad_counter;
    }
}

std::atomic<int> cas_counter{0};

void cas_increment() {
    for (int i = 0; i < 100'000; ++i) {
        int expected = cas_counter.load();
        while (!cas_counter.compare_exchange_weak(expected, expected + 1)) {
            // 重试直到成功
        }
    }
}

int main() {
    std::cout << "=== Part 1: 非原子计数器 ===\n";
    bad_counter = 0;
    std::thread t1(increment_bad);
    std::thread t2(increment_bad);
    t1.join(); t2.join();
    std::cout << "bad_counter  = " << bad_counter
              << " (expected 2000000)\n\n";

    std::cout << "=== Part 2: 原子计数器 ===\n";
    good_counter = 0;
    std::thread t3(increment_good);
    std::thread t4(increment_good);
    t3.join(); t4.join();
    std::cout << "good_counter = " << good_counter
              << " (expected 2000000)\n\n";

    std::cout << "=== Part 3: CAS 实现原子递增 ===\n";
    cas_counter = 0;
    std::thread t5(cas_increment);
    std::thread t6(cas_increment);
    t5.join(); t6.join();
    std::cout << "cas_counter  = " << cas_counter
              << " (expected 200000)\n";

    return 0;
}

// 实验02：创建和控制线程
// 对应笔记：std::thread——创建和控制线程

#include <thread>
#include <iostream>
#include <chrono>

void say_hello() {
    std::cout << "Hello from new thread!\n";
}

void worker(int id) {
    std::cout << "Worker " << id << " starting\n";
    std::cout << "Worker " << id << " done\n";
}

void slow_task() {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::cout << "Slow task done\n";
}

int main() {
    std::cout << "=== Part 1: 基本用法 ===\n";
    {
        std::thread t(say_hello);
        t.join();
        std::cout << "Hello from main thread!\n\n";
    }

    std::cout << "=== Part 2: 多线程 + 参数传递 ===\n";
    {
        std::thread t1(worker, 1);
        std::thread t2(worker, 2);
        t1.join();
        t2.join();
        std::cout << "All workers finished\n\n";
    }

    std::cout << "=== Part 3: join vs detach ===\n";
    {
        std::cout << "-- join 版本 --\n";
        std::thread t(slow_task);
        std::cout << "Waiting...\n";
        t.join();
        std::cout << "Join finished\n\n";
    }
    {
        std::cout << "-- detach 版本 --\n";
        std::thread t(slow_task);
        t.detach();
        std::cout << "Detached, main continues immediately\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
    }

    return 0;
}

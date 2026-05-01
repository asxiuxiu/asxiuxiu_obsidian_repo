// 实验05b：死锁演示
// 对应笔记：互斥锁——保护临界区
// 警告：这个程序会永远卡住！按 Ctrl+C 终止

#include <mutex>
#include <thread>
#include <iostream>
#include <chrono>

std::mutex mtx1;
std::mutex mtx2;

void thread_a() {
    std::lock_guard<std::mutex> lock1(mtx1);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::lock_guard<std::mutex> lock2(mtx2);
    std::cout << "Thread A got both locks\n";
}

void thread_b() {
    std::lock_guard<std::mutex> lock2(mtx2);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::lock_guard<std::mutex> lock1(mtx1);
    std::cout << "Thread B got both locks\n";
}

int main() {
    std::cout << "=== 死锁演示 ===\n";
    std::cout << "这个程序会永远卡住。5秒后自动退出（演示目的）。\n";
    std::cout << "真实场景中，请统一加锁顺序或使用 std::lock()\n\n";
    
    std::thread t1(thread_a);
    std::thread t2(thread_b);
    
    t1.join();
    t2.join();
    return 0;
}

#include <mutex>
#include <thread>
#include <iostream>

int data = 0;
std::mutex mtx;
bool ready = false;

void producer() {
    data = 42;
    std::lock_guard<std::mutex> lock(mtx);
    ready = true;  // 在锁保护下写
}

void consumer() {
    std::unique_lock<std::mutex> lock(mtx);
    while (!ready) { }  // 获取锁后读
    lock.unlock();
    std::cout << data << "\n";  // 保证看到 42
}
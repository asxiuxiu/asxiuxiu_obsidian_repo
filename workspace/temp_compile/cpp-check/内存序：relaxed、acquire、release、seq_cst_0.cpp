#include <atomic>
#include <thread>
#include <iostream>

std::atomic<bool> ready{false};
int data = 0;

void producer() {
    data = 42;                 // 普通写
    ready.store(true);         // 原子写
}

void consumer() {
    while (!ready.load()) { }  // 原子读
    std::cout << data << "\n"; // 普通读
}
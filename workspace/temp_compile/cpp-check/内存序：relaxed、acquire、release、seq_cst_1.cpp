#include <atomic>
#include <thread>
#include <iostream>

std::atomic<bool> ready{false};
int data = 0;

void producer() {
    data = 42;
    ready.store(true);  // 默认 seq_cst
}

void consumer() {
    while (!ready.load()) { }  // 默认 seq_cst
    std::cout << data << "\n";  // 保证看到 42
}
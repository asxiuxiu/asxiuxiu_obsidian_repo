// 实验08b：不同 Memory Fence 的性能差异
// 对应笔记：内存序——编译器和CPU如何"欺骗"你

#include <atomic>
#include <chrono>
#include <iostream>

constexpr int ITER = 100'000'000;

void bench_fence(const char* name, std::memory_order order) {
    volatile int buffer[64];
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITER; ++i) {
        buffer[i % 64] = i;
        std::atomic_thread_fence(order);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration<double, std::milli>(end - start).count();
    std::cout << name << ": " << ms << " ms\n";
}

int main() {
    std::cout << "=== Memory Fence 性能对比 ===\n";
    std::cout << "Iterations: " << ITER << "\n\n";
    bench_fence("relaxed", std::memory_order_relaxed);
    bench_fence("acq_rel", std::memory_order_acq_rel);
    bench_fence("seq_cst", std::memory_order_seq_cst);
    return 0;
}

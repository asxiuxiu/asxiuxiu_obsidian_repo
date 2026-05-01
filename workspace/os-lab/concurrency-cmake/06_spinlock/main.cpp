// 实验06：自旋锁 vs 互斥锁性能对比
// 对应笔记：自旋锁——另一种等待方式

#include <mutex>
#include <atomic>
#include <thread>
#include <vector>
#include <chrono>
#include <iostream>

constexpr int ITER = 1'000'000;
constexpr int NUM_THREADS = 4;

class Spinlock {
    std::atomic<bool> flag{false};
public:
    void lock() {
        while (flag.exchange(true, std::memory_order_acquire)) {
#if defined(__x86_64__) || defined(__i386__)
            __asm__ volatile("pause" ::: "memory");
#elif defined(__aarch64__)
            __asm__ volatile("yield" ::: "memory");
#else
            std::this_thread::yield();
#endif
        }
    }
    void unlock() { flag.store(false, std::memory_order_release); }
};

template<typename Lock>
void bench(const char* name) {
    Lock lk;
    int counter = 0;
    
    auto t1 = std::chrono::high_resolution_clock::now();
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < ITER; ++j) {
                lk.lock();
                ++counter;
                lk.unlock();
            }
        });
    }
    for (auto& t : threads) t.join();
    auto t2 = std::chrono::high_resolution_clock::now();
    
    double ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
    std::cout << name << ": " << ms << " ms (counter=" << counter << ")\n";
}

int main() {
    std::cout << "=== 自旋锁 vs 互斥锁性能对比 ===\n";
    std::cout << "Threads: " << NUM_THREADS << ", Iterations per thread: " << ITER << "\n\n";
    bench<std::mutex>("Mutex   ");
    bench<Spinlock>("Spinlock");
    return 0;
}

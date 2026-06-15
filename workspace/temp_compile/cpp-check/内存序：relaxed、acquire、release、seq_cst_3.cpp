#include <atomic>
#include <thread>
#include <iostream>

std::atomic<int> counter{0};

void worker() {
    for (int i = 0; i < 100000; ++i) {
        counter.fetch_add(1, std::memory_order_relaxed);
    }
}
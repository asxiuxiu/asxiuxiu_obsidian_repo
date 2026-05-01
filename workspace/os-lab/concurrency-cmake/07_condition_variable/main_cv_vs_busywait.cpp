// 实验07b：条件变量 vs 忙等 CPU 占用对比
// 对应笔记：条件变量——有消息了叫你
// 建议用 htop 或活动监视器观察 CPU 占用差异

#include <mutex>
#include <condition_variable>
#include <thread>
#include <iostream>
#include <chrono>
#include <atomic>

// 方案 A：条件变量
std::mutex mtx_a;
std::condition_variable cv_a;
bool ready_a = false;

void consumer_cv() {
    std::unique_lock<std::mutex> lock(mtx_a);
    cv_a.wait(lock, []() { return ready_a; });
    std::cout << "[CV] Consumer woke up!\n";
}

// 方案 B：忙等
std::atomic<bool> ready_b{false};

void consumer_busywait() {
    while (!ready_b.load()) {
        // 疯狂自旋！
    }
    std::cout << "[Busy] Consumer woke up!\n";
}

int main() {
    std::cout << "=== 条件变量 vs 忙等 ===\n";
    std::cout << "建议用 htop/活动监视器观察 CPU 占用\n\n";

    std::cout << "-- 条件变量（消费者应该几乎不占 CPU）--\n";
    {
        std::thread t(consumer_cv);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        {
            std::lock_guard<std::mutex> lock(mtx_a);
            ready_a = true;
        }
        cv_a.notify_one();
        t.join();
    }

    std::cout << "\n-- 忙等（消费者会把一个核心跑满 100%）--\n";
    {
        std::thread t(consumer_busywait);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        ready_b.store(true);
        t.join();
    }

    return 0;
}

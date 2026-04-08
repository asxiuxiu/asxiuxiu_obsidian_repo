/****************************************************************************
 * 07_multithread.cpp - 多线程调试练习
 *
 * 练习目标：
 * 1. 查看所有线程
 * 2. 切换线程
 * 3. 设置线程特定断点
 *
 * 编译和调试：
 *   g++ -g -O0 -pthread 07_multithread.cpp -o 07_multithread
 *   gdb ./07_multithread
 *
 *   (gdb) info threads          # 查看所有线程
 *   (gdb) thread 2              # 切换到线程2
 *   (gdb) thread apply all bt   # 查看所有线程的调用栈
 *   (gdb) break 35 thread 2     # 只在线程2设置断点
 ***************************************************************************/

#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <vector>
#include <sstream>

std::mutex print_mutex;

void safe_print(const std::string& msg) {
    std::lock_guard<std::mutex> lock(print_mutex);
    std::cout << msg << std::endl;
}

// 获取当前线程ID的字符串
std::string get_thread_id() {
    std::ostringstream oss;
    oss << std::this_thread::get_id();
    return oss.str();
}

void worker_task(int id, int iterations) {
    std::string thread_name = "Worker-" + std::to_string(id);
    safe_print(thread_name + " (ID: " + get_thread_id() + ") started");

    for (int i = 0; i < iterations; ++i) {
        // 在这里设置线程特定断点:
        // break 40 thread 2  (假设线程2是worker线程)

        if (i % 100 == 0) {
            std::string msg = thread_name + ": iteration " + std::to_string(i);
            safe_print(msg);
        }

        // 模拟工作
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    safe_print(thread_name + " finished");
}

void producer_consumer_demo() {
    safe_print("\n=== 生产者-消费者演示 ===");

    std::mutex mutex;
    std::condition_variable cv;
    std::vector<int> buffer;
    const int max_buffer_size = 5;
    bool done = false;

    auto producer = [&]() {
        safe_print("Producer started (ID: " + get_thread_id() + ")");

        for (int i = 0; i < 10; ++i) {
            std::unique_lock<std::mutex> lock(mutex);

            // 等待缓冲区有空间
            cv.wait(lock, [&]() { return buffer.size() < max_buffer_size; });

            buffer.push_back(i);
            safe_print("Produced: " + std::to_string(i) + ", buffer size: " + std::to_string(buffer.size()));

            lock.unlock();
            cv.notify_all();

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::unique_lock<std::mutex> lock(mutex);
        done = true;
        lock.unlock();
        cv.notify_all();

        safe_print("Producer finished");
    };

    auto consumer = [&]() {
        safe_print("Consumer started (ID: " + get_thread_id() + ")");

        while (true) {
            std::unique_lock<std::mutex> lock(mutex);

            cv.wait(lock, [&]() { return !buffer.empty() || done; });

            if (buffer.empty() && done) {
                break;
            }

            int value = buffer.back();
            buffer.pop_back();
            safe_print("Consumed: " + std::to_string(value) + ", buffer size: " + std::to_string(buffer.size()));

            lock.unlock();
            cv.notify_all();

            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }

        safe_print("Consumer finished");
    };

    std::thread prod_thread(producer);
    std::thread cons_thread(consumer);

    prod_thread.join();
    cons_thread.join();
}

void race_condition_demo() {
    safe_print("\n=== 竞态条件演示 ===");

    int counter = 0;
    const int num_increments = 10000;

    auto increment_task = [&]() {
        for (int i = 0; i < num_increments; ++i) {
            // 这里有竞态条件！多个线程同时修改 counter
            // 使用 watch counter 可以观察到谁修改了它

            int temp = counter;
            temp++;  // 模拟一些处理
            counter = temp;
        }
    };

    std::thread t1(increment_task);
    std::thread t2(increment_task);
    std::thread t3(increment_task);

    t1.join();
    t2.join();
    t3.join();

    safe_print("Expected counter: " + std::to_string(3 * num_increments));
    safe_print("Actual counter: " + std::to_string(counter));
    safe_print("Race condition caused: " + std::to_string(3 * num_increments - counter) + " lost increments");
}

void deadlock_demo() {
    safe_print("\n=== 死锁演示 ===");

    std::mutex mutex1;
    std::mutex mutex2;

    auto thread_a = [&]() {
        safe_print("Thread A: locking mutex1");
        std::lock_guard<std::mutex> lock1(mutex1);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        safe_print("Thread A: trying to lock mutex2");
        std::lock_guard<std::mutex> lock2(mutex2);  // 可能死锁

        safe_print("Thread A: got both locks");
    };

    auto thread_b = [&]() {
        safe_print("Thread B: locking mutex2");
        std::lock_guard<std::mutex> lock2(mutex2);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        safe_print("Thread B: trying to lock mutex1");
        std::lock_guard<std::mutex> lock1(mutex1);  // 可能死锁

        safe_print("Thread B: got both locks");
    };

    // 注意: 这个演示可能造成死锁，使用超时机制避免无限阻塞
    std::thread t1(thread_a);
    std::thread t2(thread_b);

    t1.join();
    t2.join();

    safe_print("Deadlock demo completed");
}

int main() {
    std::cout << "=== 多线程 GDB 调试练习 ===" << std::endl;
    std::cout << "主线程 ID: " << get_thread_id() << std::endl;

    // 练习1: 多个工作线程
    std::cout << "\n[练习1] 多个工作线程" << std::endl;
    std::cout << "使用: info threads 查看所有线程" << std::endl;
    std::cout << "使用: thread apply all bt 查看所有线程的调用栈" << std::endl;

    std::vector<std::thread> workers;
    for (int i = 0; i < 3; ++i) {
        workers.emplace_back(worker_task, i + 1, 500);
    }

    for (auto& t : workers) {
        t.join();
    }

    // 练习2: 生产者-消费者
    producer_consumer_demo();

    // 练习3: 竞态条件
    race_condition_demo();

    // 练习4: 死锁（可选，可能会阻塞）
    // deadlock_demo();

    safe_print("\n主线程结束");
    return 0;
}

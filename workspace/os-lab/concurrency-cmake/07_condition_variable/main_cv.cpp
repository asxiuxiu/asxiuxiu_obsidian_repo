// 实验07a：条件变量生产者-消费者
// 对应笔记：条件变量——有消息了叫你

#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include <iostream>

std::mutex mtx;
std::condition_variable cv;
std::queue<int> task_queue;
bool done = false;

void producer() {
    for (int i = 0; i < 10; ++i) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            task_queue.push(i);
            std::cout << "Produced: " << i << "\n";
        }
        cv.notify_one();
    }
    {
        std::lock_guard<std::mutex> lock(mtx);
        done = true;
    }
    cv.notify_all();
}

void consumer(int id) {
    while (true) {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, []() { return !task_queue.empty() || done; });
        
        if (!task_queue.empty()) {
            int task = task_queue.front();
            task_queue.pop();
            lock.unlock();
            std::cout << "Consumer " << id << " processed: " << task << "\n";
        } else if (done) {
            break;
        }
    }
}

int main() {
    std::thread p(producer);
    std::thread c1(consumer, 1);
    std::thread c2(consumer, 2);
    p.join(); c1.join(); c2.join();
    return 0;
}

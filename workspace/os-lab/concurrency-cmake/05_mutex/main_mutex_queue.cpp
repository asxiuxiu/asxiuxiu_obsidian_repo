// 实验05a：Mutex 保护队列
// 对应笔记：互斥锁——保护临界区

#include <mutex>
#include <queue>
#include <thread>
#include <iostream>

class TaskQueue {
    std::queue<int> q;
    std::mutex mtx;
public:
    void push(int value) {
        std::lock_guard<std::mutex> lock(mtx);
        q.push(value);
    }
    
    bool pop(int& value) {
        std::lock_guard<std::mutex> lock(mtx);
        if (q.empty()) return false;
        value = q.front();
        q.pop();
        return true;
    }
    
    size_t size() {
        std::lock_guard<std::mutex> lock(mtx);
        return q.size();
    }
};

TaskQueue queue;

void producer() {
    for (int i = 0; i < 1000; ++i) {
        queue.push(i);
    }
}

void consumer() {
    int value;
    int count = 0;
    while (count < 1000) {
        if (queue.pop(value)) {
            ++count;
        }
    }
}

int main() {
    std::thread t1(producer);
    std::thread t2(consumer);
    t1.join(); t2.join();
    std::cout << "Queue size = " << queue.size() << " (expected 0)\n";
    return 0;
}

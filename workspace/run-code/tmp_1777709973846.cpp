// flags: -pthread
#include <mutex>
#include <thread>
#include <iostream>

std::mutex mtx1;
std::mutex mtx2;

void thread_a() {
    std::lock_guard<std::mutex> lock1(mtx1);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::lock_guard<std::mutex> lock2(mtx2);  // 等 mtx2，但 mtx2 被 B 拿着
    std::cout << "Thread A got both locks\n";
}

void thread_b() {
    std::lock_guard<std::mutex> lock2(mtx2);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::lock_guard<std::mutex> lock1(mtx1);  // 等 mtx1，但 mtx1 被 A 拿着
    std::cout << "Thread B got both locks\n";
}

int main() {
    std::thread t1(thread_a);
    std::thread t2(thread_b);
    t1.join();  // 永远不会返回！
    t2.join();
    return 0;
}

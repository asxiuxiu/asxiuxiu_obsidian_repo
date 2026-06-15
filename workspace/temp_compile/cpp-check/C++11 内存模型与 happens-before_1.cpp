#include <thread>

int counter = 0;

void worker() {
    for (int i = 0; i < 100000; ++i) {
        ++counter;  // 读 + 改 + 写，三步
    }
}

int main() {
    std::thread t1(worker);
    std::thread t2(worker);
    t1.join();
    t2.join();
    // counter 几乎不可能是 200000
    return 0;
}
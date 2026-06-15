#include <thread>

volatile int counter = 0;

void worker() {
    for (int i = 0; i < 100000; ++i) {
        ++counter;  // 仍然不是原子操作！
    }
}
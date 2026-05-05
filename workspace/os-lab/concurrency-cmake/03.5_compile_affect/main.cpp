#include <thread>
#include <iostream>
#include <chrono>
#include <atomic>

// ============================================
// 编译器优化影响演示
// 问题：consumer 可能永远看不到 flag 的变化（死循环）
//       或者看到 flag 时 data 还没准备好（读到 0）
// ============================================

bool flag = false;
int data = 0;

// 用原子变量检测 consumer 是否已退出，用于实现“超时 join”
std::atomic<bool> consumer_done{false};

// [[gnu::noinline]] 是关键：阻止编译器把 producer/consumer 内联到 main 里。
// 这样编译器在做单线程分析时，看不到 flag/data 在“另一个函数”中被修改，
// 更容易做出“flag 不会被改变”的假设，从而触发激进优化。
[[gnu::noinline]]
void producer() {
    data = 42;

    // 制造一个明显的“时间窗口”：
    // volatile 循环不会被编译器优化掉，但它只涉及局部变量 i，
    // 不影响 flag/data 的内存访问顺序。编译器仍然可能重排 data=42 和 flag=true。
    for (volatile int i = 0; i < 80000000; ++i) { }

    flag = true;
}

[[gnu::noinline]]
void consumer() {
    // 编译器视角："这个函数里没有修改 flag 的代码，flag 是全局变量但初始为 false。
    // 既然没人改它，那我把它缓存到寄存器里，以后再也不用读内存了。"
    // 于是 while(!flag) 被优化成无限循环。
    //
    // 这里加入一个 inline asm "memory" clobber，告诉编译器：
    // "每次循环都可能读写任意内存，别做全局假设"。
    // 但这只是防止编译器把循环完全删掉；
    // 如果它仍然决定把 flag 缓存到寄存器，死循环就会出现。
    while (!flag) {
        // 空循环体——编译器最容易做激进优化
    }

    std::cout << "data = " << data << std::endl;
    consumer_done.store(true);
}

int main() {
    int deadlock_count = 0;
    int normal_count   = 0;

    for (int run = 0; run < 50; ++run) {
        // 重置状态
        flag = false;
        data = 0;
        consumer_done.store(false);

        // 先启动 consumer，确保它先进入 while(!flag) 循环
        std::thread t2(consumer);
        std::this_thread::sleep_for(std::chrono::microseconds(500));

        // 再启动 producer
        std::thread t1(producer);
        t1.detach();  // 让它自己在后台跑完

        // 等待 consumer 完成，或超时判定为死循环
        auto start = std::chrono::steady_clock::now();
        while (!consumer_done.load()) {
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= 1) {
                ++deadlock_count;
                std::cout << "[Run " << run << "] DEADLOCK! Consumer stuck. "
                          << "flag=" << flag << ", data=" << data << "\n";
                // 强制结束进程（detach 的线程也会被终止）
                std::cout << "\n===== Summary =====\n"
                          << "Total runs: " << (run + 1) << "\n"
                          << "Deadlocks:  " << deadlock_count << "\n"
                          << "Normal:     " << normal_count << "\n";
                std::terminate();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        t2.join();
        ++normal_count;
    }

    std::cout << "\n===== Summary =====\n"
              << "All 50 runs completed normally.\n"
              << "Deadlocks: " << deadlock_count << "\n"
              << "Normal:    " << normal_count << "\n"
              << "\nNote: If you see no deadlocks, try increasing the sleep "
              << "before producer or the volatile loop count.\n";
    return 0;
}

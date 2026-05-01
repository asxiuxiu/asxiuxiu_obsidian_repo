// 实验01：验证进程隔离 vs 线程共享
// 对应笔记：进程与线程——为什么需要多线程

#include <iostream>
#include <thread>

#if defined(__linux__) || defined(__APPLE__)
#include <unistd.h>
#include <sys/wait.h>
#endif

int global_var = 42;

void test_process_isolation() {
#if defined(__linux__)
    pid_t pid = fork();
    if (pid == 0) {
        global_var = 999;
        std::cout << "[子进程] global_var = " << global_var << "\n";
        _exit(0);
    } else {
        wait(nullptr);
        std::cout << "[父进程] global_var = " << global_var << "\n";
    }
    std::cout << "  => 结论：子进程修改不影响父进程（进程隔离）\n\n";
#else
    std::cout << "[跳过] fork() 仅在 Linux 上演示，macOS/Windows 请直接看线程共享测试\n\n";
#endif
}

void test_thread_sharing() {
    global_var = 42;
    std::thread t([]() {
        global_var = 777;
        std::cout << "[子线程] global_var = " << global_var << "\n";
    });
    t.join();
    std::cout << "[主线程] global_var = " << global_var << "\n";
    std::cout << "  => 结论：子线程修改主线程立刻可见（共享内存）\n";
}

int main() {
    std::cout << "=== 实验01：进程隔离 vs 线程共享 ===\n\n";
    test_process_isolation();
    test_thread_sharing();
    return 0;
}

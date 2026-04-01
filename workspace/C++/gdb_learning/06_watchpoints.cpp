/****************************************************************************
 * 06_watchpoints.cpp - 观察点(Watchpoint)练习
 *
 * 练习目标：
 * 1. 使用 watch 监控变量写入
 * 2. 使用 rwatch 监控变量读取
 * 3. 使用 awatch 监控变量访问
 *
 * 调试命令：
 *   g++ -g -O0 06_watchpoints.cpp -o 06_watchpoints
 *   gdb ./06_watchpoints
 *
 *   (gdb) watch global_var      # 变量被写入时暂停
 *   (gdb) rwatch secret_value   # 变量被读取时暂停
 *   (gdb) awatch shared_data    # 变量被读写时暂停
 *   (gdb) watch ptr if *ptr > 100  # 条件观察点
 ***************************************************************************/

#include <iostream>
#include <cstring>

// 全局变量，用于演示 watchpoint
int global_counter = 0;
int secret_code = 0;

class DataMonitor {
private:
    int value;
    std::string name;

public:
    DataMonitor(const std::string& n, int v) : name(n), value(v) {
        std::cout << "DataMonitor '" << name << "' created with value " << value << std::endl;
    }

    void setValue(int v) {
        std::cout << "  Setting " << name << " from " << value << " to " << v << std::endl;
        value = v;  // 在这里设置 watchpoint: watch value
    }

    int getValue() const {
        return value;  // 在这里设置 rwatch: rwatch value
    }

    void increment() {
        ++value;
    }

    void display() const {
        std::cout << "  " << name << " = " << value << std::endl;
    }
};

void watch_demo() {
    std::cout << "\n=== Watch 演示 (监控写入) ===" << std::endl;

    // 在 GDB 中: watch global_counter
    // 每当 global_counter 被修改时，GDB 会暂停

    std::cout << "准备修改 global_counter..." << std::endl;

    for (int i = 0; i < 5; ++i) {
        ++global_counter;
        std::cout << "global_counter = " << global_counter << std::endl;
    }

    global_counter = 100;
    std::cout << "global_counter 直接设置为 " << global_counter << std::endl;
}

void read_watch_demo() {
    std::cout << "\n=== Rwatch 演示 (监控读取) ===" << std::endl;

    // 在 GDB 中: rwatch secret_code
    // 每当 secret_code 被读取时，GDB 会暂停

    secret_code = 12345;
    std::cout << "secret_code 已设置" << std::endl;

    // 下面的读取操作会触发 rwatch
    int temp1 = secret_code;
    std::cout << "temp1 = " << temp1 << std::endl;

    int temp2 = secret_code * 2;
    std::cout << "temp2 = " << temp2 << std::endl;

    if (secret_code > 0) {  // 这里也会读取
        std::cout << "secret_code is positive" << std::endl;
    }
}

void access_watch_demo() {
    std::cout << "\n=== Awatch 演示 (监控读写) ===" << std::endl;

    int shared_resource = 0;

    // 在 GDB 中: awatch shared_resource
    // 任何读写操作都会触发

    std::cout << "写入 shared_resource..." << std::endl;
    shared_resource = 10;  // 写入 - 触发

    std::cout << "读取 shared_resource: " << shared_resource << std::endl;  // 读取 - 触发

    shared_resource += 5;  // 先读取再写入 - 触发两次

    std::cout << "最终值: " << shared_resource << std::endl;
}

void conditional_watch_demo() {
    std::cout << "\n=== 条件 Watchpoint 演示 ===" << std::endl;

    int threshold = 50;

    // 在 GDB 中: watch threshold if threshold > 100
    // 只有当 threshold 的值大于100时才会触发

    for (int i = 0; i < 20; ++i) {
        threshold += 10;
        std::cout << "threshold = " << threshold << std::endl;

        if (threshold > 150) {
            std::cout << "Threshold exceeded 150!" << std::endl;
        }
    }
}

void pointer_watch_demo() {
    std::cout << "\n=== 指针 Watchpoint 演示 ===" << std::endl;

    int data[10] = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90};
    int* ptr = &data[0];

    // 监控指针指向的内容:
    // watch *ptr     # 监控 ptr 指向的值
    // watch ptr      # 监控指针本身（地址变化）

    std::cout << "初始值: *ptr = " << *ptr << std::endl;

    for (int i = 0; i < 10; ++i) {
        *ptr = i * 100;  // 修改指向的值
        std::cout << "data[" << i << "] = " << *ptr << std::endl;
        ptr++;  // 移动指针
    }
}

void object_watch_demo() {
    std::cout << "\n=== 对象成员 Watchpoint 演示 ===" << std::endl;

    DataMonitor monitor("Temperature", 20);

    // 在 GDB 中:
    // watch monitor    # 监控对象内部变化
    // 或使用: set var monitor.value 来修改内部值

    monitor.display();

    for (int i = 0; i < 5; ++i) {
        monitor.increment();
        monitor.display();
    }

    monitor.setValue(100);
    monitor.display();
}

// 模拟内存被意外修改的场景
void memory_corruption_demo() {
    std::cout << "\n=== 内存被意外修改演示 ===" << std::endl;

    int important_value = 42;
    int buffer[10];

    std::cout << "important_value 初始值: " << important_value << std::endl;

    // 使用 watch 监控 important_value
    // 然后运行下面的代码，看是谁修改了它

    // 模拟越界写入（bug）
    for (int i = 0; i <= 10; ++i) {  // 注意: i <= 10 会导致越界
        buffer[i] = 999;  // 当 i==10 时，可能覆盖 important_value
    }

    std::cout << "important_value 最终值: " << important_value << std::endl;
    if (important_value != 42) {
        std::cout << "WARNING: important_value 被意外修改了!" << std::endl;
    }
}

int main() {
    std::cout << "=== Watchpoint GDB 练习 ===" << std::endl;

    watch_demo();
    read_watch_demo();
    access_watch_demo();
    conditional_watch_demo();
    pointer_watch_demo();
    object_watch_demo();
    memory_corruption_demo();

    std::cout << "\n程序正常结束" << std::endl;
    return 0;
}

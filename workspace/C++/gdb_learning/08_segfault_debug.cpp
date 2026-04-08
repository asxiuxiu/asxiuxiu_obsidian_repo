/****************************************************************************
 * 08_segfault_debug.cpp - 段错误调试练习
 *
 * 练习目标：
 * 1. 分析 Core Dump
 * 2. 使用 backtrace 定位崩溃位置
 * 3. 检查空指针和越界访问
 *
 * 编译和调试：
 *   g++ -g -O0 -pthread 08_segfault_debug.cpp -o 08_segfault_debug
 *
 *   # 启用 core dump
 *   ulimit -c unlimited
 *
 *   ./08_segfault_debug
 *
 *   # 程序崩溃后分析 core 文件
 *   gdb ./08_segfault_debug core
 *   (gdb) bt                        # 查看崩溃时的调用栈
 *   (gdb) info locals               # 查看局部变量
 *   (gdb) info registers            # 查看寄存器
 *   (gdb) list                      # 查看崩溃位置代码
 *
 *   # 或者在 GDB 中直接运行
 *   gdb ./08_segfault_debug
 *   (gdb) run
 *   (gdb) bt                        # 崩溃后查看调用栈
 ***************************************************************************/

#include <iostream>
#include <cstring>
#include <vector>

// Bug 1: 空指针解引用
void null_pointer_bug() {
    std::cout << "\n=== Bug 1: 空指针解引用 ===" << std::endl;

    int* ptr = nullptr;

    std::cout << "即将解引用空指针..." << std::endl;

    // 这会导致 SIGSEGV (段错误)
    // *ptr = 42;

    // 为了避免立即崩溃，注释掉了上面的代码
    // 取消注释来观察段错误

    std::cout << "空指针测试跳过（取消注释 *ptr = 42 来测试）" << std::endl;
}

// Bug 2: 数组越界
void array_out_of_bounds() {
    std::cout << "\n=== Bug 2: 数组越界 ===" << std::endl;

    int arr[5] = {1, 2, 3, 4, 5};
    int safe_value = 100;

    std::cout << "数组地址: " << arr << std::endl;
    std::cout << "safe_value 地址: " << &safe_value << std::endl;

    // 越界写入 - 可能损坏栈上的其他变量
    for (int i = 0; i <= 10; ++i) {
        arr[i] = 999;  // 当 i >= 5 时越界
        std::cout << "Writing arr[" << i << "] = 999" << std::endl;
    }

    std::cout << "safe_value 被意外修改为: " << safe_value << std::endl;
}

// Bug 3: 使用已释放的内存
void use_after_free() {
    std::cout << "\n=== Bug 3: 使用已释放的内存 ===" << std::endl;

    int* ptr = new int(42);
    std::cout << "Allocated value: " << *ptr << std::endl;

    delete ptr;
    std::cout << "Memory freed" << std::endl;

    // 危险：使用已释放的内存
    // *ptr = 100;  // 可能导致崩溃或数据损坏

    std::cout << "Use after free 测试跳过（取消注释 *ptr = 100 来测试）" << std::endl;
}

// Bug 4: 双重释放
void double_free() {
    std::cout << "\n=== Bug 4: 双重释放 ===" << std::endl;

    int* ptr = new int(42);

    delete ptr;
    std::cout << "First delete completed" << std::endl;

    // 危险：双重释放
    // delete ptr;  // 会导致运行时错误

    std::cout << "Double free 测试跳过（取消注释第二个 delete 来测试）" << std::endl;
}

// Bug 5: 返回局部变量地址
int* return_local_address() {
    int local_var = 42;
    std::cout << "Local var address: " << &local_var << std::endl;

    // 危险：返回局部变量的地址
    // return &local_var;

    return nullptr;  // 安全做法
}

// Bug 6: 栈溢出
void stack_overflow_bug(int depth) {
    char large_array[10000];  // 10KB 的局部数组

    // 防止优化掉 unused 变量
    large_array[0] = (char)depth;

    std::cout << "Stack depth: " << depth << ", array address: " << (void*)large_array << std::endl;

    // 递归调用直到栈溢出
    if (depth < 1000) {
        stack_overflow_bug(depth + 1);
    }
}

// Bug 7: 除以零
void divide_by_zero() {
    std::cout << "\n=== Bug 7: 除以零 ===" << std::endl;

    int a = 10;
    int b = 0;

    std::cout << "a = " << a << ", b = " << b << std::endl;

    // 这会导致浮点异常 (SIGFPE)
    // int result = a / b;
    // std::cout << "Result: " << result << std::endl;

    std::cout << "Divide by zero 测试跳过（取消注释 int result = a / b 来测试）" << std::endl;
}

// Bug 8: 字符串格式化错误（可能导致崩溃）
void format_string_bug() {
    std::cout << "\n=== Bug 8: 格式化字符串 Bug ===" << std::endl;

    char buffer[100];
    const char* user_input = "%s%s%s%s%s%s%s%s";  // 恶意的格式化字符串

    // 危险：直接使用用户输入作为格式字符串
    // sprintf(buffer, user_input);

    // 安全做法
    snprintf(buffer, sizeof(buffer), "%s", user_input);
    std::cout << "Buffer content: " << buffer << std::endl;
}

// 复杂的崩溃场景
class DataProcessor {
public:
    void process(int* data, int size) {
        std::cout << "Processing data at " << data << ", size=" << size << std::endl;

        // 这里可能传入空指针
        for (int i = 0; i < size; ++i) {
            data[i] *= 2;
        }
    }
};

void complex_crash_scenario() {
    std::cout << "\n=== 复杂崩溃场景 ===" << std::endl;

    DataProcessor processor;

    // 场景1: 正常调用
    int data1[] = {1, 2, 3, 4, 5};
    processor.process(data1, 5);

    // 场景2: 传入空指针（会导致崩溃）
    // processor.process(nullptr, 10);

    std::cout << "复杂场景测试跳过（取消注释 processor.process(nullptr, 10) 来测试）" << std::endl;
}

// 安全的内存操作示例
void safe_memory_practices() {
    std::cout << "\n=== 安全的内存操作示例 ===" << std::endl;

    // 使用智能指针避免内存泄漏
    // 使用 std::vector 避免数组越界
    // 使用引用避免空指针

    std::vector<int> safe_vector = {1, 2, 3, 4, 5};

    // 安全的遍历
    for (size_t i = 0; i < safe_vector.size(); ++i) {
        safe_vector[i] *= 2;
    }

    // 使用范围 for 循环更安全
    for (auto& val : safe_vector) {
        std::cout << val << " ";
    }
    std::cout << std::endl;
}

int main() {
    std::cout << "=== 段错误调试 GDB 练习 ===" << std::endl;
    std::cout << "提示: 使用 ulimit -c unlimited 启用 core dump" << std::endl;

    // 依次运行各种 bug 演示
    // 取消注释来测试不同的崩溃场景

    null_pointer_bug();
    array_out_of_bounds();
    use_after_free();
    double_free();

    int* dangling_ptr = return_local_address();
    if (dangling_ptr) {
        std::cout << "Dangling pointer value: " << *dangling_ptr << std::endl;
    }

    // 注意: 栈溢出会导致立即崩溃
    // std::cout << "\n测试栈溢出（可能导致立即崩溃）..." << std::endl;
    // stack_overflow_bug(0);

    divide_by_zero();
    format_string_bug();
    complex_crash_scenario();
    safe_memory_practices();

    std::cout << "\n程序结束" << std::endl;
    return 0;
}

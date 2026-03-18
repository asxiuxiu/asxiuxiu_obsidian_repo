/****************************************************************************
 * 01_basic_debug.cpp - GDB 基础调试练习
 *
 * 练习目标：
 * 1. 使用 break 设置断点
 * 2. 使用 run/continue 运行程序
 * 3. 使用 next/step 单步执行
 * 4. 使用 print 查看变量
 *
 * 调试命令：
 *   g++ -g -O0 01_basic_debug.cpp -o 01_basic_debug
 *   gdb ./01_basic_debug
 *
 *   (gdb) break main          # 在 main 函数设置断点
 *   (gdb) run                 # 运行程序
 *   (gdb) next                # 单步执行，不进入函数
 *   (gdb) print a             # 查看变量 a
 *   (gdb) step                # 单步执行，进入函数
 *   (gdb) continue            # 继续运行
 ***************************************************************************/

#include <iostream>
#include <vector>

// 计算两个数的和
int add(int x, int y) {
    int result = x + y;
    std::cout << "Inside add(): " << x << " + " << y << " = " << result << std::endl;
    return result;
}

// 计算阶乘
int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

int main() {
    std::cout << "=== GDB 基础调试练习 ===" << std::endl;

    // 练习1: 基本变量观察
    int a = 10;
    int b = 20;
    int sum = 0;

    std::cout << "\n[练习1] 基本变量观察" << std::endl;
    std::cout << "a = " << a << ", b = " << b << std::endl;

    sum = add(a, b);  // 在这里使用 'step' 进入函数

    std::cout << "Sum = " << sum << std::endl;

    // 练习2: 循环调试
    std::cout << "\n[练习2] 循环调试" << std::endl;
    std::vector<int> numbers = {1, 2, 3, 4, 5};
    int total = 0;

    for (size_t i = 0; i < numbers.size(); ++i) {
        total += numbers[i];
        // 可以尝试在这里设置条件断点: break 48 if i == 3
    }
    std::cout << "Total = " << total << std::endl;

    // 练习3: 函数调用链
    std::cout << "\n[练习3] 递归调试" << std::endl;
    int fact_result = factorial(5);
    std::cout << "5! = " << fact_result << std::endl;

    std::cout << "\n程序正常结束" << std::endl;
    return 0;
}

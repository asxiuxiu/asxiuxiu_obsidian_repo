/****************************************************************************
 * 04_call_stack.cpp - 调用栈分析练习
 *
 * 练习目标：
 * 1. 使用 backtrace (bt) 查看调用栈
 * 2. 使用 up/down 切换栈帧
 * 3. 理解函数调用过程
 *
 * 调试命令：
 *   g++ -g -O0 04_call_stack.cpp -o 04_call_stack
 *   gdb ./04_call_stack
 *
 *   (gdb) break function_d
 *   (gdb) run
 *   (gdb) backtrace           # 查看完整调用栈
 *   (gdb) bt 5                # 显示前5层
 *   (gdb) frame 2             # 切换到第2层栈帧
 *   (gdb) up                  # 向上一层
 *   (gdb) down                # 向下一层
 *   (gdb) info locals         # 查看当前帧的局部变量
 *   (gdb) info args           # 查看当前帧的参数
 ***************************************************************************/

#include <iostream>

// 模拟一个深度调用链，用于练习 backtrace

void function_d(int d_param) {
    std::cout << "\n=== 在 function_d 中 ===" << std::endl;
    std::cout << "d_param = " << d_param << std::endl;

    // 在这里设置断点，然后使用 backtrace 查看调用链
    int local_d = d_param * 4;
    std::cout << "local_d = " << local_d << std::endl;

    // 使用 GDB 命令:
    // backtrace 或 bt
    // info locals
    // info args
    // frame 0, frame 1, frame 2, frame 3
    // up, down
}

void function_c(int c_param) {
    std::cout << "进入 function_c" << std::endl;
    int local_c = c_param * 3;

    function_d(local_c);

    std::cout << "离开 function_c" << std::endl;
}

void function_b(int b_param) {
    std::cout << "进入 function_b" << std::endl;
    int local_b = b_param * 2;

    function_c(local_b);

    std::cout << "离开 function_b" << std::endl;
}

void function_a(int a_param) {
    std::cout << "进入 function_a" << std::endl;
    int local_a = a_param * 1;

    function_b(local_a);

    std::cout << "离开 function_a" << std::endl;
}

// 递归函数 - 观察递归调用栈
int recursive_factorial(int n, int depth) {
    // 打印缩进表示调用深度
    std::string indent(depth * 2, ' ');
    std::cout << indent << "recursive_factorial(" << n << ") depth=" << depth << std::endl;

    if (n <= 1) {
        std::cout << indent << "Base case reached!" << std::endl;
        return 1;
    }

    int result = n * recursive_factorial(n - 1, depth + 1);
    std::cout << indent << "Returning " << result << std::endl;
    return result;
}

// 模拟复杂的调用关系
class Calculator {
public:
    int multiply(int a, int b) {
        int result = 0;
        for (int i = 0; i < b; ++i) {
            result = add(result, a);
        }
        return result;
    }

    int add(int a, int b) {
        return a + b;  // 在这里设置断点
    }

    int power(int base, int exp) {
        if (exp == 0) return 1;
        if (exp == 1) return base;
        return multiply(base, power(base, exp - 1));
    }
};

int main() {
    std::cout << "=== 调用栈分析 GDB 练习 ===" << std::endl;

    std::cout << "\n[练习1] 简单调用链" << std::endl;
    std::cout << "调用链: main -> function_a -> function_b -> function_c -> function_d" << std::endl;
    std::cout << "在 function_d 中设置断点，使用 backtrace 查看调用栈" << std::endl;

    function_a(5);

    std::cout << "\n[练习2] 递归调用栈" << std::endl;
    std::cout << "观察递归函数的调用栈增长" << std::endl;

    int fact = recursive_factorial(5, 0);
    std::cout << "5! = " << fact << std::endl;

    std::cout << "\n[练习3] 类方法调用栈" << std::endl;
    Calculator calc;
    int result = calc.multiply(3, 4);
    std::cout << "3 * 4 = " << result << std::endl;

    int pow_result = calc.power(2, 5);
    std::cout << "2^5 = " << pow_result << std::endl;

    std::cout << "\n程序正常结束" << std::endl;
    return 0;
}

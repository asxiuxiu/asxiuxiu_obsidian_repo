// main.cpp - 主程序入口
#include <iostream>
#include "utils.h"

int main() {
    std::cout << "=== C++ 编译选项实践 ===" << std::endl;

    int a = 10, b = 20;
    std::cout << "add(10, 20) = " << add(a, b) << std::endl;
    std::cout << "multiply(10, 20) = " << multiply(a, b) << std::endl;

    print_version();

#ifdef DEBUG
    std::cout << "[DEBUG] 调试模式已开启" << std::endl;
#endif

#ifdef VERSION
    std::cout << "[VERSION] 版本: " << VERSION << std::endl;
#endif

    return 0;
}

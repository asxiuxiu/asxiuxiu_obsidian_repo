// From note: C++/构建系统：make 与 CMake
#include <cmath>   // 在 <iostream> 前包含，避免 macOS/libc++ 下 hypot/sqrt 等 using 声明冲突
#include <iostream>
#include "utils.h"
#include "mymath.h"

int main() {
    std::cout << "Hello from MyProject!" << std::endl;

    // 测试 utils 和 math 模块
    greet("World");
    std::cout << "1 + 2 = " << add(1, 2) << std::endl;

    return 0;
}

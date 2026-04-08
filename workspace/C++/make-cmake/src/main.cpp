// From note: C++/构建系统：make 与 CMake
//
// 演示：CMake 同时链接静态库（mymath）和动态库（mystr）
//   - libs/mymath → STATIC，编译时代码直接合并进本可执行文件
//   - libs/mystr  → SHARED，运行时加载 libmystr.dylib / libmystr.so
//
// 两个库的 CMakeLists 都用了 PUBLIC target_include_directories，
// 所以这里直接 include 头文件名，无需手动添加 -I 路径。

#include <iostream>
#include "mymath.h"   // 来自 libs/mymath/include/ — 静态库
#include "mystr.h"    // 来自 libs/mystr/include/  — 动态库

int main() {
    std::cout << "=== CMake 静态库 & 动态库 实践 ===" << std::endl;

    // --- 静态库 mymath ---
    std::cout << "\n[静态库 mymath]" << std::endl;
    std::cout << "  power(2, 10)  = " << mymath::power(2, 10) << std::endl;
    std::cout << "  gcd(48, 18)   = " << mymath::gcd(48, 18)  << std::endl;
    std::cout << "  lcm(4, 6)     = " << mymath::lcm(4, 6)    << std::endl;

    // --- 动态库 mystr ---
    std::cout << "\n[动态库 mystr]" << std::endl;
    std::string s = "Hello, CMake!";
    std::cout << "  原字符串        : " << s                         << std::endl;
    std::cout << "  to_upper        : " << mystr::to_upper(s)        << std::endl;
    std::cout << "  to_lower        : " << mystr::to_lower(s)        << std::endl;
    std::cout << "  count_char('l') : " << mystr::count_char(s, 'l') << std::endl;
    std::cout << "  reverse_str     : " << mystr::reverse_str(s)     << std::endl;

    return 0;
}

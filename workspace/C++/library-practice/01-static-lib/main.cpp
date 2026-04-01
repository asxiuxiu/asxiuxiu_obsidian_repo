// ============================================================
// 使用静态库 libmymath.a 的示例程序
//
// 编译方式（手动）：
//   cd 01-static-lib
//   g++ -c src/power.cpp src/gcd_lcm.cpp src/prime.cpp src/fibonacci.cpp -Iinclude
//   ar rcs build/libmymath.a power.o gcd_lcm.o prime.o fibonacci.o
//   g++ main.cpp -Iinclude -Lbuild -lmymath -o build/demo
//   ./build/demo
//
// 或者直接 make static
// ============================================================

#include <iostream>
#include "mymath.h"

int main() {
    std::cout << "========== libmymath 静态库演示 ==========\n\n";

    // --- 快速幂 ---
    std::cout << "[power] 2^10 = " << mymath::power(2, 10) << "\n";
    std::cout << "[power] 3^5  = " << mymath::power(3, 5)  << "\n";

    // --- GCD / LCM ---
    std::cout << "\n[gcd]   gcd(48, 18) = " << mymath::gcd(48, 18) << "\n";
    std::cout << "[lcm]   lcm(4, 6)   = " << mymath::lcm(4, 6)   << "\n";

    // --- 质数判断 ---
    std::cout << "\n[prime] 前 20 个质数：";
    int count = 0;
    for (int n = 2; count < 20; ++n) {
        if (mymath::is_prime(n)) {
            std::cout << n << " ";
            ++count;
        }
    }
    std::cout << "\n";

    // --- 斐波那契 ---
    std::cout << "\n[fib]   fibonacci(0..10): ";
    for (int i = 0; i <= 10; ++i) {
        std::cout << mymath::fibonacci(i) << " ";
    }
    std::cout << "\n";

    return 0;
}

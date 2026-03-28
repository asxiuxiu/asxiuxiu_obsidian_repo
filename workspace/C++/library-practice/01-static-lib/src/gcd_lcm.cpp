#include "mymath.h"

namespace mymath {

int gcd(int a, int b) {
    // 欧几里得辗转相除
    // gcd(12, 8) → gcd(8, 4) → gcd(4, 0) → 4
    while (b != 0) {
        int t = b;
        b = a % b;
        a = t;
    }
    return a;
}

int lcm(int a, int b) {
    // lcm × gcd = a × b，先除后乘避免溢出
    return a / gcd(a, b) * b;
}

} // namespace mymath

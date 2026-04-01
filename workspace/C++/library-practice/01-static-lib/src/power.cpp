#include "mymath.h"

namespace mymath {

// 快速幂：O(log exp)
long long power(long long base, int exp) {
    long long result = 1;
    while (exp > 0) {
        if (exp % 2 == 1) {
            result *= base;
        }
        base *= base;
        exp /= 2;
    }
    return result;
}

} // namespace mymath

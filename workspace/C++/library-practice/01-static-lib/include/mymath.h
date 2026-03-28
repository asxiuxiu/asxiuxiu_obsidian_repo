#pragma once

// ============================================================
// libmymath — 自写静态库练习
// 提供基础数学工具函数
// ============================================================

namespace mymath {

// 幂运算：base ^ exp（整数版）
long long power(long long base, int exp);

// 最大公约数（欧几里得算法）
int gcd(int a, int b);

// 最小公倍数
int lcm(int a, int b);

// 判断是否为质数
bool is_prime(int n);

// 斐波那契数列第 n 项（n 从 0 开始）
long long fibonacci(int n);

} // namespace mymath

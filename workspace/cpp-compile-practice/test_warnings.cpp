// test_warnings.cpp - 用于测试警告选项的示例
#include <iostream>

// 这个函数会产生一些警告
void test_warnings() {
    int x = 10;
    int y;          // 未使用变量 -Wunused
    int x = 20;     // 变量遮蔽 -Wshadow (故意写错，编译会报错)

    double d = 3.14;
    int i = d;      // 隐式类型转换 -Wconversion

    (void)i; // 防止未使用警告
}

int main() {
    test_warnings();
    return 0;
}

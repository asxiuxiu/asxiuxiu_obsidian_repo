// Day 1: 写一个 String 类（只含 char* 和 size_t）
// 核心考察点：构造函数、析构函数、深拷贝
// 自检：能解释为什么析构函数要 delete[]，能画出内存布局图

#include "common.h"
#include <cstring>

// TODO: 在此实现你的 String 类
// class String {
//     char* data_;
//     size_t len_;
// public:
//     String(const char* str);  // 构造函数
//     ~String();                // 析构函数
// };

// -------------------- 测试用例 --------------------

void test_default_constructor() {
    // TODO: 测试构造
}

void test_destructor() {
    // TODO: 测试析构后内存不泄漏
}

void test_memory_layout() {
    // TODO: 打印 &obj, data_ 指针地址，画出内存布局
}

int main() {
    RUN_TEST(test_default_constructor);
    RUN_TEST(test_destructor);
    RUN_TEST(test_memory_layout);

    std::cout << "All tests passed!" << std::endl;
    return 0;
}

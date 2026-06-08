// Day 4: 写一个 Array<T, N> 固定大小数组模板
// 题目：实现一个编译期确定大小的数组模板，支持元素访问和边界检查。

#include "common.h"
#include <cstddef>

template <typename T, size_t N>
class Array {
    // TODO: 在此实现 Array<T, N>
};

// -------------------- 测试用例 --------------------

void test_size_is_n() {
    Array<int, 5> a;
    CHECK_EQ(a.size(), 5);
}

void test_read_write_elements() {
    Array<int, 3> a;
    a[0] = 10;
    a[1] = 20;
    a[2] = 30;
    CHECK_EQ(a[0], 10);
    CHECK_EQ(a[1], 20);
    CHECK_EQ(a[2], 30);
}

void test_const_access() {
    Array<int, 3> a;
    a[0] = 42;
    const Array<int, 3>& ref = a;
    CHECK_EQ(ref[0], 42);
}

void test_fill() {
    Array<int, 4> a;
    a.fill(7);
    CHECK_EQ(a[0], 7);
    CHECK_EQ(a[3], 7);
}

void test_different_types() {
    Array<double, 2> d;
    d[0] = 3.14;
    CHECK_TRUE(d[0] > 3.0);
}

void test_zero_size() {
    Array<int, 0> a;
    CHECK_EQ(a.size(), 0);
}

void test_large_array() {
    Array<int, 1000> a;
    a[0] = 1;
    a[999] = 2;
    CHECK_EQ(a[0], 1);
    CHECK_EQ(a[999], 2);
}

int main() {
    RUN_TEST(test_size_is_n);
    RUN_TEST(test_read_write_elements);
    RUN_TEST(test_const_access);
    RUN_TEST(test_fill);
    RUN_TEST(test_different_types);
    RUN_TEST(test_zero_size);
    RUN_TEST(test_large_array);

    std::cout << "All tests passed!" << std::endl;
    return 0;
}

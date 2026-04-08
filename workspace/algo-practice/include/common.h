#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <cassert>

// ============================================================
// 轻量测试宏（无需引入 gtest）
// ============================================================
#define TEST_CASE(name) void name()
#define RUN_TEST(name) do { \
    std::cout << "[RUN ] " #name << std::endl; \
    name(); \
    std::cout << "[PASS] " #name << std::endl; \
} while(0)

#define EXPECT_EQ(a, b) do { \
    if ((a) != (b)) { \
        std::cerr << "  FAIL at " __FILE__ ":" << __LINE__ \
                  << " expected " << (b) << " got " << (a) << std::endl; \
        std::exit(1); \
    } \
} while(0)

#define EXPECT_VEC_EQ(a, b) do { \
    if ((a) != (b)) { \
        std::cerr << "  FAIL at " __FILE__ ":" << __LINE__ << " vector mismatch\n"; \
        std::exit(1); \
    } \
} while(0)

// 打印 vector（调试用）
template<typename T>
void print_vec(const std::vector<T>& v, const std::string& label = "") {
    if (!label.empty()) std::cout << label << ": ";
    std::cout << "[";
    bool first = true;
    for (const auto& x : v) {
        if (!first) std::cout << ", ";
        std::cout << x;
        first = false;
    }
    std::cout << "]\n";
}

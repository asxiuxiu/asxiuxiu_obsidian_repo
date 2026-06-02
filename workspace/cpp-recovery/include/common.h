#pragma once

#include <iostream>
#include <cassert>

// 简单测试宏，避免引入 gtest 增加复杂度
#define CHECK_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            std::cerr << "CHECK_EQ failed at " << __FILE__ << ":" << __LINE__ \
                      << " expected " << (b) << " got " << (a) << std::endl; \
            std::exit(1); \
        } \
    } while(0)

#define CHECK_TRUE(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "CHECK_TRUE failed at " << __FILE__ << ":" << __LINE__ \
                      << ": " << #expr << std::endl; \
            std::exit(1); \
        } \
    } while(0)

#define RUN_TEST(name) \
    do { \
        std::cout << "[RUN] " << #name << std::endl; \
        name(); \
        std::cout << "[PASS] " << #name << std::endl; \
    } while(0)

// Day 1: 写一个 String 类（只含 char* 和 size_t）
// 核心考察点：构造函数、析构函数、深拷贝
// 自检：能解释为什么析构函数要 delete[]，能画出内存布局图

#include "common.h"
#include <cstring>


class String {
    char* data_;
    size_t len_;
public:
    String(const char* str)
        : data_(nullptr), len_(0)
    {
        if (str == nullptr) return;

        len_ = std::strlen(str);
        data_ = new char[len_ + 1];
        std::memcpy(data_, str, len_);
        data_[len_] = '\0';
    }

    String(const String& other) :data_(nullptr), len_(0)
    {
        if (other.data_ == nullptr)
        {
            return;
        }
        len_ = other.len_;
        data_ = new char[len_ + 1];
        std::memcpy(data_, other.data_, len_ + 1);
    }

    ~String()
    {
        delete[] data_;
    }

    size_t size() const { return len_; }
    const char* c_str() const { return data_; }
};

// -------------------- 测试用例 --------------------

void test_normal_construction() {
    String s("hello");
    CHECK_EQ(s.size(), 5);
    CHECK_TRUE(std::strcmp(s.c_str(), "hello") == 0);
}

void test_nullptr_construction() {
    String s(nullptr);
    CHECK_EQ(s.size(), 0);
    CHECK_TRUE(s.c_str() == nullptr);
}

void test_empty_string_size_is_zero() {
    String s("");
    CHECK_EQ(s.size(), 0);
    CHECK_TRUE(std::strcmp(s.c_str(), "") == 0);
}

void test_destructor_no_crash() {
    // 只要在作用域结束时不崩溃、不泄漏，就算通过
    { String s("will be destroyed"); }
    { String s(nullptr); }
    { String s(""); }
}

void test_memory_layout() {
    String s("abc");
    std::cout << "  &s        = " << &s << std::endl;
    std::cout << "  s.c_str() = " << static_cast<const void*>(s.c_str()) << std::endl;
    std::cout << "  (stack obj vs heap data)" << std::endl;
}

void test_copy_construction_normal() {
    String s1("hello");
    String s2(s1);
    CHECK_EQ(s2.size(), 5);
    CHECK_TRUE(std::strcmp(s2.c_str(), "hello") == 0);
}

void test_copy_construction_from_nullptr() {
    String s1(nullptr);
    String s2(s1);
    CHECK_EQ(s2.size(), 0);
    CHECK_TRUE(s2.c_str() == nullptr);
}

void test_copy_construction_from_empty() {
    String s1("");
    String s2(s1);
    CHECK_EQ(s2.size(), 0);
    CHECK_TRUE(std::strcmp(s2.c_str(), "") == 0);
}

void test_copy_is_deep_not_shallow() {
    String s1("abc");
    String s2(s1);
    // 两个对象指向不同的堆内存地址
    CHECK_TRUE(s1.c_str() != s2.c_str());
    CHECK_TRUE(std::strcmp(s1.c_str(), s2.c_str()) == 0);
}

void test_destructor_after_copy_no_crash() {
    {
        String s1("will be destroyed twice");
        String s2(s1);
        // s1 和 s2 各自管理独立内存，析构时不会 double free
    }
    {
        String s1(nullptr);
        String s2(s1);
    }
}

int main() {
    RUN_TEST(test_normal_construction);
    RUN_TEST(test_nullptr_construction);
    RUN_TEST(test_empty_string_size_is_zero);
    RUN_TEST(test_destructor_no_crash);
    RUN_TEST(test_memory_layout);
    RUN_TEST(test_copy_construction_normal);
    RUN_TEST(test_copy_construction_from_nullptr);
    RUN_TEST(test_copy_construction_from_empty);
    RUN_TEST(test_copy_is_deep_not_shallow);
    RUN_TEST(test_destructor_after_copy_no_crash);

    std::cout << "All tests passed!" << std::endl;
    return 0;
}

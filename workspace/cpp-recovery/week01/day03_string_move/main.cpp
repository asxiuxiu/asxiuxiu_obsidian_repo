// Day 3: 给 String 加上移动构造和移动赋值
// 题目：在 Day 2 的基础上，实现移动构造函数和移动赋值运算符。

#include "common.h"
#include <cstring>
#include <utility>

class String {
    char *data_;
    size_t len_;

  public:
    String(const char *str) : data_(nullptr), len_(0) {
        if (str == nullptr)
            return;

        len_ = std::strlen(str);
        data_ = new char[len_ + 1];
        std::memcpy(data_, str, len_);
        data_[len_] = '\0';
    }

    String(const String &other) : data_(nullptr), len_(other.len_) {
        if (other.data_ == nullptr)
            return;
        data_ = new char[len_ + 1];
        std::memcpy(data_, other.data_, len_ + 1);
    }

    String &operator=(const String &other) {
        if (this == &other) {
            return *this;
        }

        if (other.data_ == nullptr) {
            delete[] data_;
            data_ = nullptr;
            len_ = 0;
            return *this;
        }

        char *tmp = data_;

        data_ = new char[other.len_ + 1];
        delete[] tmp;
        len_ = other.len_;

        std::memcpy(data_, other.data_, len_ + 1);

        return *this;
    }

    String(String &&other) noexcept : data_(other.data_), len_(other.len_) {
        other.data_ = nullptr;
        other.len_ = 0;
    }

    String &operator=(String &&other) noexcept {
        if (this == &other) {
            return *this;
        }

        delete[] data_;
        data_ = other.data_;
        len_ = other.len_;

        other.data_ = nullptr;
        other.len_ = 0;

        return *this;
    }

    ~String() { delete[] data_; }

    size_t size() const { return len_; }
    const char *c_str() const { return data_; }
};

// -------------------- 测试用例 --------------------

void test_move_constructor_transfers_ownership() {
    String s1("hello");
    String s2(std::move(s1));
    CHECK_EQ(s2.size(), 5);
    CHECK_TRUE(std::strcmp(s2.c_str(), "hello") == 0);
}

void test_move_constructor_from_empty() {
    String s1("");
    String s2(std::move(s1));
    CHECK_EQ(s2.size(), 0);
}

void test_move_assignment_transfers_ownership() {
    String s1("hello");
    String s2("world");
    s2 = std::move(s1);
    CHECK_EQ(s2.size(), 5);
    CHECK_TRUE(std::strcmp(s2.c_str(), "hello") == 0);
}

void test_move_assignment_overwrites_old_data() {
    String s1("short");
    String s2("this is a much longer string");
    s2 = std::move(s1);
    CHECK_TRUE(std::strcmp(s2.c_str(), "short") == 0);
}

void test_move_assignment_self() {
    String s("self");
    s = std::move(s);
    CHECK_EQ(s.size(), 4);
    CHECK_TRUE(std::strcmp(s.c_str(), "self") == 0);
}

void test_moved_from_is_destructible_and_assignable() {
    String s1("hello");
    String s2(std::move(s1));
    s1 = String("reassigned");
    CHECK_TRUE(std::strcmp(s1.c_str(), "reassigned") == 0);
}

void test_move_noexcept() {
    CHECK_TRUE(std::is_nothrow_move_constructible_v<String>);
    CHECK_TRUE(std::is_nothrow_move_assignable_v<String>);
}

void test_move_assignment_chain() {
    String s1("a");
    String s2("b");
    String s3("c");
    s1 = s2 = std::move(s3);
    CHECK_TRUE(std::strcmp(s1.c_str(), "c") == 0);
    CHECK_TRUE(std::strcmp(s2.c_str(), "c") == 0);
}

void test_destructor_after_move_no_crash() {
    {
        String s1("first");
        String s2(std::move(s1));
    }
}

int main() {
    RUN_TEST(test_move_constructor_transfers_ownership);
    RUN_TEST(test_move_constructor_from_empty);
    RUN_TEST(test_move_assignment_transfers_ownership);
    RUN_TEST(test_move_assignment_overwrites_old_data);
    RUN_TEST(test_move_assignment_self);
    RUN_TEST(test_moved_from_is_destructible_and_assignable);
    RUN_TEST(test_move_noexcept);
    RUN_TEST(test_move_assignment_chain);
    RUN_TEST(test_destructor_after_move_no_crash);

    std::cout << "All tests passed!" << std::endl;
    return 0;
}

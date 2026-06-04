// Day 2: 给 String 加上拷贝赋值运算符
// 题目：在 Day 1 的基础上，让 String 类支持赋值操作。

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

    String(const String& other)
        : data_(nullptr), len_(0)
    {
        if (other.data_ == nullptr) return;
        len_ = other.len_;
        data_ = new char[len_ + 1];
        std::memcpy(data_, other.data_, len_ + 1);
    }

    const String& operator=(const String& other)
    {
        if (this == &other)
        {
            return *this;
        }

        if (other.len_ == this->len_) {

        }


        return *this;
    }

    ~String()
    {
        delete[] data_;
    }

    size_t size() const { return len_; }
    const char* c_str() const { return data_; }
};

// -------------------- 测试用例 --------------------

void test_copy_assignment_normal() {
    String s1("hello");
    String s2("world");
    s2 = s1;
    CHECK_EQ(s2.size(), 5);
    CHECK_TRUE(std::strcmp(s2.c_str(), "hello") == 0);
}

void test_self_assignment_safe() {
    String s("self");
    s = s;
    CHECK_EQ(s.size(), 4);
    CHECK_TRUE(std::strcmp(s.c_str(), "self") == 0);
}

void test_copy_assignment_is_deep() {
    String s1("abc");
    String s2("def");
    s2 = s1;
    CHECK_TRUE(s1.c_str() != s2.c_str());
    CHECK_TRUE(std::strcmp(s1.c_str(), s2.c_str()) == 0);
}

void test_chained_assignment() {
    String s1("a");
    String s2("b");
    String s3("c");
    s1 = s2 = s3;
    CHECK_TRUE(std::strcmp(s1.c_str(), "c") == 0);
    CHECK_TRUE(std::strcmp(s2.c_str(), "c") == 0);
    CHECK_TRUE(std::strcmp(s3.c_str(), "c") == 0);
}

void test_assignment_from_empty() {
    String s1("hello");
    String s2("");
    s1 = s2;
    CHECK_EQ(s1.size(), 0);
    CHECK_TRUE(std::strcmp(s1.c_str(), "") == 0);
}

void test_assignment_from_nullptr_state() {
    String s1("hello");
    String s2(nullptr);
    s1 = s2;
    CHECK_EQ(s1.size(), 0);
    CHECK_TRUE(s1.c_str() == nullptr);
}

void test_assignment_overwrites_old_data() {
    String s1("short");
    String s2("this is a much longer string");
    s1 = s2;
    CHECK_TRUE(std::strcmp(s1.c_str(), "this is a much longer string") == 0);
}

void test_destructor_after_assignment_no_crash() {
    {
        String s1("first");
        String s2("second");
        s1 = s2;
    }
}

int main() {
    RUN_TEST(test_copy_assignment_normal);
    RUN_TEST(test_self_assignment_safe);
    RUN_TEST(test_copy_assignment_is_deep);
    RUN_TEST(test_chained_assignment);
    RUN_TEST(test_assignment_from_empty);
    RUN_TEST(test_assignment_from_nullptr_state);
    RUN_TEST(test_assignment_overwrites_old_data);
    RUN_TEST(test_destructor_after_assignment_no_crash);

    std::cout << "All tests passed!" << std::endl;
    return 0;
}

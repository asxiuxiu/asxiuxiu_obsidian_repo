#include "solution.h"
#include <iostream>
#include "../include/common.h"

using namespace algo;

int main() {
    Solution sol;
    bool pass = true;

    // 测试用例1: 基本异位词
    RUN_TEST(pass, sol.isAnagram("anagram", "nagaram") == true);
    
    // 测试用例2: 不是异位词
    RUN_TEST(pass, sol.isAnagram("rat", "car") == false);
    
    // 测试用例3: 长度不同
    RUN_TEST(pass, sol.isAnagram("a", "ab") == false);
    
    // 测试用例4: 空字符串
    RUN_TEST(pass, sol.isAnagram("", "") == true);
    
    // 测试用例5: 单个字符
    RUN_TEST(pass, sol.isAnagram("a", "a") == true);

    if (pass) {
        std::cout << "All tests passed!" << std::endl;
        return 0;
    } else {
        std::cout << "Some tests failed!" << std::endl;
        return 1;
    }
}
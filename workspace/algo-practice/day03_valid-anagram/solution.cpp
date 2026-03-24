// LeetCode: https://leetcode.com/problems/valid-anagram/

#include "solution.h"

namespace algo {

bool Solution::isAnagram(std::string s, std::string t) {
    // TODO: 实现字符计数解法
    // 思路：
    // 1. 如果长度不同，直接返回 false
    // 2. 使用一个长度为26的数组记录每个字符出现的次数
    // 3. 遍历字符串 s，增加计数
    // 4. 遍历字符串 t，减少计数
    // 5. 检查所有计数是否为0
    return false;
}

} // namespace algo
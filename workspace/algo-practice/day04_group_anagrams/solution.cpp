
// LeetCode: https://leetcode.com/problems/group-anagrams/

#include "solution.h"
#include <unordered_map>
#include <algorithm>

namespace group_anagrams {

// TODO: 实现哈希表分组算法
// 核心思路：
// 1. 对每个字符串排序，变位词排序后相同
// 2. 用排序后的字符串作为 key，原字符串列表作为 value
// 3. 返回所有分组
//
// 优化思路：
// - 可以用字符计数（26个字母）作为 key，避免排序的 O(K log K)
// - 或者用 26 个质数相乘作为唯一 key

std::vector<std::vector<std::string>> Solution::groupAnagrams(std::vector<std::string>& strs) {
    // TODO: 请在这里实现你的解法
    (void)strs;  // 暂时消除未使用警告
    return {};
}

} // namespace group_anagrams

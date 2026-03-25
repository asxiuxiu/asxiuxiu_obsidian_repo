// LeetCode 242: Valid Anagram
// https://leetcode.com/problems/valid-anagram/
// 
// 题目描述：
// 给定两个字符串 s 和 t，编写一个函数来判断 t 是否是 s 的字母异位词。
// 字母异位词是通过重新排列不同单词或短语的字母而形成的单词或短语，
// 恰好使用原始字母一次。

#pragma once

#include <string>

namespace algo {

class Solution {
public:
    bool isAnagram(std::string s, std::string t);
};

} // namespace algo
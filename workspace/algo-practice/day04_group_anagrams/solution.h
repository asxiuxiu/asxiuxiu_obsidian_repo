
#pragma once
#include <vector>
#include <string>

// ============================================================
// Day 04 - Group Anagrams (LeetCode #49)
// 难度: Medium | 核心: 哈希表分组
//
// 游戏场景: 技能归类——把变位词技能组合归为一类
//   想象你有一个技能系统，每个技能由一组符文组成。
//   "fire" 和 "erfi" 是同一组符文的不同排列（变位词），
//   应该归为同一类技能，方便玩家查询和学习。
//
// 题目:
//   给定字符串数组 strs，将变位词组合在一起。
//   可以按任意顺序返回结果列表。
//
// 示例:
//   输入: strs = ["eat","tea","tan","ate","nat","bat"]
//   输出: [["bat"],["nat","tan"],["ate","eat","tea"]]
// ============================================================

namespace group_anagrams {

class Solution {
public:
    std::vector<std::vector<std::string>> groupAnagrams(std::vector<std::string>& strs);
};

} // namespace group_anagrams

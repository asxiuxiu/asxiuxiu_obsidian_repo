#pragma once
#include <vector>

// ============================================================
// Day 01 - Two Sum (LeetCode #1)
// 难度: Easy | 核心: 哈希表空间换时间
//
// 游戏场景: 背包里找两件装备凑成指定攻击力
//
// 题目:
//   给定整数数组 nums 和目标值 target，
//   找出和为 target 的两个元素的下标。
//   假设每种输入只对应一个答案，不能重复使用同一个元素。
//
// 示例:
//   输入: nums = [2,7,11,15], target = 9
//   输出: [0,1]
// ============================================================

class Solution {
public:
    std::vector<int> twoSum(std::vector<int>& nums, int target);
};

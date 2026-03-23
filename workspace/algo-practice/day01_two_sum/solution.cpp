#include "solution.h"
#include <unordered_map>

// LeetCode: https://leetcode.com/problems/two-sum/

// ============================================================
// 解法：哈希表一次遍历
//
// 思路：
//   遍历数组时，用哈希表记录「已见过的值 → 下标」。
//   对每个元素 nums[i]，查找 complement = target - nums[i]
//   是否在哈希表中。若存在，直接返回两个下标。
//
// 时间复杂度: O(n)
// 空间复杂度: O(n)
//
// 游戏类比:
//   背包里找两件装备凑攻击力。遍历装备时，查"已见过的装备表"，
//   O(1) 查找比 O(n) 逐一对比快得多，帧率敏感场景优先哈希表。
// ============================================================

std::vector<int> Solution::twoSum(std::vector<int>& nums, int target) {
    // TODO: 在此完成你的解法
    // 提示: 使用 std::unordered_map<int, int> seen; 记录 值->下标

    std::unordered_map<int, int> seen;
    for (int i = 0; i < (int)nums.size(); ++i) {
        int complement = target - nums[i];
        if (seen.count(complement)) {
            return {seen[complement], i};
        }
        seen[nums[i]] = i;
    }
    return {}; // 题目保证有解，不会到达这里
}

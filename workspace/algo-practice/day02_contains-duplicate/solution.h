#pragma once

#include <vector>

namespace leetcode217 {

/**
 * @brief 217. Contains Duplicate
 * 
 * 给定一个整数数组，判断是否存在重复元素。
 * 如果存在一值在数组中出现至少两次，函数返回 true；
 * 如果数组中每个元素互不相同，返回 false。
 * 
 * 示例 1：
 * Input: nums = [1,2,3,1]
 * Output: true
 * 
 * 示例 2：
 * Input: nums = [1,2,3,4]
 * Output: false
 * 
 * 示例 3：
 * Input: nums = [1,1,1,3,3,4,3,2,4,2]
 * Output: true
 * 
 * 约束条件：
 * - 1 <= nums.length <= 10^5
 * - -10^9 <= nums[i] <= 10^9
 */
class Solution {
public:
    bool containsDuplicate(std::vector<int>& nums);
};

} // namespace leetcode217

#include "solution.h"
#include <unordered_map>

// LeetCode: https://leetcode.com/problems/two-sum/
//
// 练习任务：实现 twoSum 函数
// 目标：找到数组中和为目标值的两个数的下标
//
// 💡 提示：
// 1. 考虑使用 std::unordered_map 建立「数值→下标」的映射
// 2. 遍历数组时，计算 complement = target - 当前数值
// 3. 检查 complement 是否已在 map 中
// 4. 如果找到，返回两个下标；否则将当前数值加入 map
// 5. 注意返回的下标顺序：[complement的下标, 当前下标]
//
// 🎮 游戏场景思考：
// 想象你在设计一个 RPG 的技能组合系统
// 玩家需要找到两个技能，它们的"组合值"恰好等于目标伤害
// 你需要返回这两个技能在技能栏中的位置

std::vector<int> Solution::twoSum(std::vector<int> &nums, int target) {
    // TODO: 在这里实现你的解法
    //
    // 步骤建议：
    // 1. 创建 unordered_map<int, int> 存储「数值→下标」
    // 2. 用 for 循环遍历 nums，i 作为下标
    // 3. 计算 complement = target - nums[i]
    // 4. 用 find() 检查 complement 是否在 map 中
    // 5. 如果找到，return {map[complement], i}
    // 6. 如果没找到，map[nums[i]] = i
    // 7. 循环结束 return {} (理论上一定有解)
    
    return {};  // 占位符，请删除这行并实现你的代码
}

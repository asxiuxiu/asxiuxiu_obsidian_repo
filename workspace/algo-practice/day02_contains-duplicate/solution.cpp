// LeetCode: https://leetcode.com/problems/contains-duplicate/
// 
// 练习任务：实现 containsDuplicate 函数
// 目标：判断数组中是否存在重复元素
//
// 💡 提示：
// 1. 考虑使用 std::unordered_set 来记录已经见过的数字
// 2. 遍历数组时，检查当前数字是否已在集合中
// 3. 如果存在，返回 true；否则加入集合并继续
// 4. 遍历结束都没找到重复，返回 false
//
// 🎮 游戏场景思考：
// 想象你在检查玩家背包里的物品ID，需要快速发现是否有重复物品
// 这关系到游戏经济系统的防复制检测

#include "solution.h"
#include <unordered_set>

namespace leetcode217 {

bool Solution::containsDuplicate(std::vector<int>& nums) {
    // TODO: 在这里实现你的解法
    // 
    // 步骤建议：
    // 1. 创建一个 unordered_set<int> 用于存储已见过的数字
    // 2. 用范围for循环遍历 nums
    // 3. 用 find() 检查数字是否已存在
    // 4. 如果找到，return true
    // 5. 如果没找到，insert() 到集合
    // 6. 循环结束 return false
    
    return false;  // 占位符，请删除这行并实现你的代码
}

} // namespace leetcode217

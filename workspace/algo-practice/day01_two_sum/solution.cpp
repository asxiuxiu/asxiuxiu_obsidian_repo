#include "solution.h"
#include <unordered_map>

// LeetCode: https://leetcode.com/problems/two-sum/

std::vector<int> Solution::twoSum(std::vector<int> &nums, int target) {
  std::unordered_map<int, int> num_to_index;
  for (size_t i = 0; i < nums.size(); i++) {
    if (num_to_index.find(target - nums[i]) != num_to_index.end()) {
      return {num_to_index[target - nums[i]], static_cast<int>(i)};
    } else {
      num_to_index[nums[i]] = i;
    }
  }
  return {};
}

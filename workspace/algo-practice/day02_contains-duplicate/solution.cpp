// LeetCode: https://leetcode.com/problems/contains-duplicate/

#include "solution.h"
#include <unordered_set>

namespace leetcode217 {

bool Solution::containsDuplicate(std::vector<int>& nums) {
    std::unordered_set<int> seen;
    
    for (int num : nums) {
        if (seen.find(num) != seen.end()) {
            return true;
        }
        seen.insert(num);
    }
    
    return false;
}

} // namespace leetcode217

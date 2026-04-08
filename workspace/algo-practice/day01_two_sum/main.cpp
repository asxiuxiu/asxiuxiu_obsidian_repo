#include "solution.h"
#include "common.h"
#include <algorithm>

Solution sol;

// ---- 测试用例 -----------------------------------------------

TEST_CASE(test_basic) {
    std::vector<int> nums = {2, 7, 11, 15};
    auto result = sol.twoSum(nums, 9);
    std::sort(result.begin(), result.end());
    std::vector<int> expected = {0, 1};
    EXPECT_VEC_EQ(result, expected);
}

TEST_CASE(test_middle) {
    std::vector<int> nums = {3, 2, 4};
    auto result = sol.twoSum(nums, 6);
    std::sort(result.begin(), result.end());
    std::vector<int> expected = {1, 2};
    EXPECT_VEC_EQ(result, expected);
}

TEST_CASE(test_same_value) {
    std::vector<int> nums = {3, 3};
    auto result = sol.twoSum(nums, 6);
    std::sort(result.begin(), result.end());
    std::vector<int> expected = {0, 1};
    EXPECT_VEC_EQ(result, expected);
}

TEST_CASE(test_negative) {
    std::vector<int> nums = {-1, -2, -3, -4, -5};
    auto result = sol.twoSum(nums, -8);
    std::sort(result.begin(), result.end());
    std::vector<int> expected = {2, 4};
    EXPECT_VEC_EQ(result, expected);
}

// ---- 主函数 -------------------------------------------------

int main() {
    std::cout << "=== Day 01: Two Sum ===\n";
    RUN_TEST(test_basic);
    RUN_TEST(test_middle);
    RUN_TEST(test_same_value);
    RUN_TEST(test_negative);
    std::cout << "\n所有测试通过!\n";
    return 0;
}


#include "solution.h"
#include "common.h"
#include <algorithm>

using namespace group_anagrams;

Solution sol;

// ---- 测试用例 -----------------------------------------------

TEST_CASE(test_basic) {
    std::vector<std::string> strs = {"eat", "tea", "tan", "ate", "nat", "bat"};
    auto result = sol.groupAnagrams(strs);
    // 预期3组：[bat], [nat,tan], [ate,eat,tea]
    EXPECT_EQ(result.size(), 3u);
}

TEST_CASE(test_single) {
    std::vector<std::string> strs = {"a"};
    auto result = sol.groupAnagrams(strs);
    EXPECT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].size(), 1u);
    EXPECT_EQ(result[0][0], "a");
}

TEST_CASE(test_all_same) {
    std::vector<std::string> strs = {"aaa", "aaa", "aaa"};
    auto result = sol.groupAnagrams(strs);
    // 所有都是变位词，应该在同一组
    EXPECT_EQ(result.size(), 1u);
}

TEST_CASE(test_no_anagrams) {
    std::vector<std::string> strs = {"abc", "def", "ghi"};
    auto result = sol.groupAnagrams(strs);
    // 没有变位词，每组一个
    EXPECT_EQ(result.size(), 3u);
}

TEST_CASE(test_empty_strings) {
    std::vector<std::string> strs = {"", "", ""};
    auto result = sol.groupAnagrams(strs);
    // 空字符串也是变位词
    EXPECT_EQ(result.size(), 1u);
}

// ---- 主函数 -------------------------------------------------

int main() {
    std::cout << "=== Day 04: Group Anagrams ===\n";
    RUN_TEST(test_basic);
    RUN_TEST(test_single);
    RUN_TEST(test_all_same);
    RUN_TEST(test_no_anagrams);
    RUN_TEST(test_empty_strings);
    std::cout << "\n所有测试通过!\n";
    return 0;
}

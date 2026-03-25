#include "solution.h"
#include "../include/common.h"
#include <iostream>

using namespace leetcode217;

TEST_CASE("Contains Duplicate - Basic Cases") {
    Solution sol;
    
    SUBCASE("Example 1 - Has Duplicate") {
        std::vector<int> nums = {1, 2, 3, 1};
        CHECK(sol.containsDuplicate(nums) == true);
    }
    
    SUBCASE("Example 2 - No Duplicate") {
        std::vector<int> nums = {1, 2, 3, 4};
        CHECK(sol.containsDuplicate(nums) == false);
    }
    
    SUBCASE("Example 3 - Multiple Duplicates") {
        std::vector<int> nums = {1, 1, 1, 3, 3, 4, 3, 2, 4, 2};
        CHECK(sol.containsDuplicate(nums) == true);
    }
}

TEST_CASE("Contains Duplicate - Edge Cases") {
    Solution sol;
    
    SUBCASE("Single Element") {
        std::vector<int> nums = {1};
        CHECK(sol.containsDuplicate(nums) == false);
    }
    
    SUBCASE("Two Same Elements") {
        std::vector<int> nums = {1, 1};
        CHECK(sol.containsDuplicate(nums) == true);
    }
    
    SUBCASE("Two Different Elements") {
        std::vector<int> nums = {1, 2};
        CHECK(sol.containsDuplicate(nums) == false);
    }
    
    SUBCASE("Negative Numbers") {
        std::vector<int> nums = {-1, -2, -3, -1};
        CHECK(sol.containsDuplicate(nums) == true);
    }
    
    SUBCASE("Large Range") {
        std::vector<int> nums = {-1000000000, 0, 1000000000, -1000000000};
        CHECK(sol.containsDuplicate(nums) == true);
    }
}

TEST_CASE("Contains Duplicate - Game Scenarios") {
    Solution sol;
    
    SUBCASE("Backpack Item IDs - No Duplicates") {
        // 模拟背包中的物品ID，都是唯一的
        std::vector<int> itemIDs = {1001, 1002, 1003, 1004, 1005};
        CHECK(sol.containsDuplicate(itemIDs) == false);
    }
    
    SUBCASE("Backpack Item IDs - Has Duplicates (Stackable Items)") {
        // 模拟背包中有可堆叠物品（ID重复表示数量）
        std::vector<int> itemIDs = {2001, 2002, 2001, 2003};
        CHECK(sol.containsDuplicate(itemIDs) == true);
    }
}

int main() {
    // 运行 doctest
    doctest::Context context;
    context.run();
    
    // 打印游戏映射说明
    std::cout << "\n🎮 游戏映射说明：\n";
    std::cout << "想象你在检查玩家背包——需要快速判断背包里有没有重复的物品ID。\n";
    std::cout << "如果是MMORPG，这可能关系到装备是否被复制；\n";
    std::cout << "如果是生存游戏，这可能影响资源堆叠逻辑。\n";
    
    return 0;
}

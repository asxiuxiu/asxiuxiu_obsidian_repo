/****************************************************************************
 * 05_conditional_breakpoints.cpp - 条件断点练习
 *
 * 练习目标：
 * 1. 设置条件断点
 * 2. 使用 ignore 跳过断点
 * 3. 使用 tbreak 设置临时断点
 *
 * 调试命令：
 *   g++ -g -O0 05_conditional_breakpoints.cpp -o 05_conditional_breakpoints
 *   gdb ./05_conditional_breakpoints
 *
 *   (gdb) break 45 if i == 50       # 条件断点
 *   (gdb) break 45 if i % 10 == 0   # 复杂条件
 *   (gdb) ignore 1 10               # 跳过前10次
 *   (gdb) tbreak 60                 # 临时断点（只生效一次）
 ***************************************************************************/

#include <iostream>
#include <vector>
#include <cstdlib>
#include <ctime>

void loop_demo() {
    std::cout << "\n=== 循环条件断点演示 ===" << std::endl;

    // 场景1: 在特定迭代暂停
    std::cout << "\n场景1: 查找第50次迭代" << std::endl;
    for (int i = 0; i < 100; ++i) {
        // 使用: break 27 if i == 50
        // 而不是在第27行设置普通断点并手动继续50次
        if (i % 25 == 0) {
            std::cout << "Iteration " << i << std::endl;
        }
    }

    // 场景2: 每隔N次暂停
    std::cout << "\n场景2: 每隔10次迭代" << std::endl;
    for (int j = 0; j < 100; ++j) {
        // 使用: break 36 if j % 10 == 0
        // 或: break 36 if j == 10 || j == 20 || j == 30
        int value = j * j;
        if (j % 20 == 0) {
            std::cout << "j=" << j << ", value=" << value << std::endl;
        }
    }
}

void search_demo() {
    std::cout << "\n=== 搜索条件断点演示 ===" << std::endl;

    std::vector<int> data;
    srand(time(nullptr));

    // 生成随机数据
    for (int i = 0; i < 1000; ++i) {
        data.push_back(rand() % 1000);
    }

    // 搜索特定值
    int target = 777;
    bool found = false;

    for (size_t i = 0; i < data.size(); ++i) {
        // 使用条件断点: break 58 if data[i] == 777
        // 或: break 58 if i > 500 && data[i] > 900
        if (data[i] == target) {
            std::cout << "Found " << target << " at index " << i << std::endl;
            found = true;
            break;
        }
    }

    if (!found) {
        std::cout << target << " not found in data" << std::endl;
    }
}

void binary_search_demo() {
    std::cout << "\n=== 二分搜索调试演示 ===" << std::endl;

    int arr[] = {2, 5, 8, 12, 16, 23, 38, 56, 72, 91};
    int size = sizeof(arr) / sizeof(arr[0]);
    int target = 23;

    int left = 0;
    int right = size - 1;

    while (left <= right) {
        int mid = left + (right - left) / 2;

        // 在这里设置条件断点观察算法过程:
        // break 82 if mid == 5        # 检查特定中间位置
        // break 82 if arr[mid] == 23  # 找到目标值时暂停

        std::cout << "left=" << left << ", mid=" << mid << ", right=" << right;
        std::cout << ", arr[mid]=" << arr[mid] << std::endl;

        if (arr[mid] == target) {
            std::cout << "Found at index " << mid << std::endl;
            return;
        }

        if (arr[mid] < target) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }

    std::cout << "Not found" << std::endl;
}

void ignore_count_demo() {
    std::cout << "\n=== Ignore 计数演示 ===" << std::endl;

    // 场景: 前99次调用正常，第100次出问题
    for (int i = 1; i <= 200; ++i) {
        // 在 GDB 中:
        // 1. break 107
        // 2. run
        // 3. ignore 1 99    # 忽略前99次
        // 4. continue
        // 程序会在第100次迭代时暂停

        if (i == 100) {
            std::cout << " *** This is iteration 100 ***" << std::endl;
        }

        if (i % 50 == 0) {
            std::cout << "Processing iteration " << i << std::endl;
        }
    }
}

void temporary_breakpoint_demo() {
    std::cout << "\n=== 临时断点演示 ===" << std::endl;

    // 临时断点只生效一次，然后自动删除
    for (int round = 1; round <= 3; ++round) {
        std::cout << "\nRound " << round << std::endl;

        for (int i = 0; i < 5; ++i) {
            // 使用 tbreak 127 设置临时断点
            // 程序会在这里暂停一次，然后断点自动删除
            std::cout << "  Step " << i << std::endl;
        }
    }
}

int main() {
    std::cout << "=== 条件断点 GDB 练习 ===" << std::endl;

    loop_demo();
    search_demo();
    binary_search_demo();
    ignore_count_demo();
    temporary_breakpoint_demo();

    std::cout << "\n程序正常结束" << std::endl;
    return 0;
}

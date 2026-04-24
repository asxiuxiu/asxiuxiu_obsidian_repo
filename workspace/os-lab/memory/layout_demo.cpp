#include <iostream>
#include <vector>
#include <algorithm>
#include <iomanip>

int global_init = 42;             // .data
int global_uninit;                // .bss
const char* msg = "Hello";        // .data (指针), "Hello" 在 .rodata

void func() {                     // .text
    static int local_static = 1;  // .data
    int local_stack = 2;          // 栈
    int* local_heap = new int(3); // 堆

    // 收集所有变量信息：(地址, 名称, 所属段)
    std::vector<std::pair<uintptr_t, std::string>> regions;

    regions.push_back({reinterpret_cast<uintptr_t>(&global_init),    "global_init    (.data)"});
    regions.push_back({reinterpret_cast<uintptr_t>(&global_uninit),  "global_uninit  (.bss)"});
    regions.push_back({reinterpret_cast<uintptr_t>(&local_static),  "local_static   (.data)"});
    regions.push_back({reinterpret_cast<uintptr_t>(&local_stack),   "local_stack    (栈)"});
    regions.push_back({reinterpret_cast<uintptr_t>(local_heap),     "local_heap     (堆)"});
    regions.push_back({reinterpret_cast<uintptr_t>(msg),            "msg(指针值)    (.data)"});
    regions.push_back({reinterpret_cast<uintptr_t>(func),           "func(代码地址) (.text)"});

    // 按地址从高到低排序
    std::sort(regions.begin(), regions.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    std::cout << "===== 内存地址排序（高地址 → 低地址） =====\n";
    std::cout << std::hex << std::uppercase;

    for (const auto& [addr, name] : regions) {
        std::cout << "0x" << std::setw(16) << std::setfill('0') << addr
                  << "  " << name << "\n";
    }

    std::cout << std::dec; // 恢复十进制
    delete local_heap;
}

int main() {
    func();
    return 0;
}

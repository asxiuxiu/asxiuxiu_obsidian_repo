// ============================================================
// demo_fmt.cpp — 使用 {fmt} 开源库
//
// fmt 是 C++20 std::format 的前身，格式化语法类似 Python f-string
// 安装方式：brew install fmt
// GitHub：  https://github.com/fmtlib/fmt
//
// 编译（手动）：
//   g++ -std=c++17 src/demo_fmt.cpp \
//       -I/usr/local/include \
//       -L/usr/local/lib -lfmt \
//       -o build/demo_fmt
//   DYLD_LIBRARY_PATH=/usr/local/lib ./build/demo_fmt
//
// 或者：make fmt
// ============================================================

#include <fmt/format.h>
#include <fmt/color.h>
#include <fmt/ranges.h>

#include <vector>
#include <string>
#include <cmath>

int main() {
    // ----------------------------------------------------------
    // 1. 基础格式化
    // ----------------------------------------------------------
    fmt::print("========== {{fmt}} 开源库演示 ==========\n\n");

    // 对比 printf：类型安全，不会因为格式符写错而崩溃
    fmt::print("[基础] Hello, {}! You are {} years old.\n", "Alice", 30);

    // ----------------------------------------------------------
    // 2. 数字格式化
    // ----------------------------------------------------------
    fmt::print("\n[数字格式化]\n");
    fmt::print("  十六进制: {:#x}\n",  255);     // 0xff
    fmt::print("  八进制:  {:#o}\n",  255);     // 0377
    fmt::print("  二进制:  {:#b}\n",  255);     // 0b11111111
    fmt::print("  浮点精度: {:.4f}\n", 3.14159265);
    fmt::print("  科学计数: {:e}\n",  12345.678);
    fmt::print("  右对齐:  {:>10d}\n", 42);
    fmt::print("  补零:    {:08d}\n",  42);

    // ----------------------------------------------------------
    // 3. 字符串对齐
    // ----------------------------------------------------------
    fmt::print("\n[字符串对齐]\n");
    fmt::print("  左对齐: |{:<15}|\n", "hello");
    fmt::print("  右对齐: |{:>15}|\n", "hello");
    fmt::print("  居中:   |{:^15}|\n", "hello");
    fmt::print("  填充:   |{:*^15}|\n", "hello");

    // ----------------------------------------------------------
    // 4. 格式化容器（fmt::ranges）
    // ----------------------------------------------------------
    fmt::print("\n[容器格式化]\n");
    std::vector<int> nums = {1, 2, 3, 4, 5};
    fmt::print("  vector<int>:    {}\n", nums);

    std::vector<std::string> words = {"foo", "bar", "baz"};
    fmt::print("  vector<string>: {}\n", words);

    // ----------------------------------------------------------
    // 5. fmt::format 返回 std::string
    // ----------------------------------------------------------
    fmt::print("\n[返回 string]\n");
    std::string msg = fmt::format("圆面积 r={} → {:.2f}", 5, M_PI * 25);
    fmt::print("  {}\n", msg);

    // ----------------------------------------------------------
    // 6. 彩色终端输出
    // ----------------------------------------------------------
    fmt::print("\n[彩色输出]\n");
    fmt::print(fmt::fg(fmt::color::green),  "  成功信息（绿色）\n");
    fmt::print(fmt::fg(fmt::color::red),    "  错误信息（红色）\n");
    fmt::print(fmt::fg(fmt::color::yellow), "  警告信息（黄色）\n");
    fmt::print(fmt::fg(fmt::color::cyan) | fmt::emphasis::bold,
               "  加粗青色文字\n");

    // ----------------------------------------------------------
    // 7. 实际应用：打印对齐表格
    // ----------------------------------------------------------
    fmt::print("\n[应用：打印表格]\n");
    fmt::print("  {:<10} {:>8} {:>8}\n", "Name", "Score", "Grade");
    fmt::print("  {:-<10} {:->8} {:->8}\n", "", "", "");

    struct Student { std::string name; int score; char grade; };
    std::vector<Student> students = {
        {"Alice", 95, 'A'},
        {"Bob",   82, 'B'},
        {"Carol", 76, 'C'},
    };
    for (const auto& s : students) {
        fmt::print("  {:<10} {:>8} {:>8c}\n", s.name, s.score, s.grade);
    }

    return 0;
}

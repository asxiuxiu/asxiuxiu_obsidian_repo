// ============================================================
// 使用 {fmt} 开源库的示例程序
//
// {fmt} 是 C++20 std::format 的前身，格式化语法类似 Python f-string
// GitHub: https://github.com/fmtlib/fmt
// 文档:   https://fmt.dev
//
// 本示例使用 header-only 模式（无需编译 fmt 本身）
// 编译命令：
//   g++ -std=c++17 src/main.cpp -Ivendor/fmt/include -o build/demo
//   ./build/demo
//
// 或者：make -C 03-open-source
// ============================================================

// header-only 模式：包含这个头文件就行，不需要链接 .a/.so
#define FMT_HEADER_ONLY
#include <fmt/format.h>
#include <fmt/color.h>
#include <fmt/ranges.h>

#include <vector>
#include <map>
#include <string>
#include <cmath>

int main() {
    // ----------------------------------------------------------
    // 1. 基础格式化（对比 printf 和 cout）
    // ----------------------------------------------------------
    fmt::print("========== {{fmt}} 开源库演示 ==========\n\n");

    // printf 风格：类型不安全，占位符繁琐
    // printf("Hello, %s! You are %d years old.\n", "Alice", 30);

    // fmt 风格：类型安全、可读性强
    fmt::print("[基础] Hello, {}! You are {} years old.\n", "Alice", 30);

    // ----------------------------------------------------------
    // 2. 数字格式化：宽度、精度、进制
    // ----------------------------------------------------------
    fmt::print("\n[数字格式化]\n");
    fmt::print("  十进制: {:d}\n",  255);
    fmt::print("  十六进制: {:#x}\n", 255);   // 0xff
    fmt::print("  八进制:  {:#o}\n", 255);   // 0377
    fmt::print("  二进制:  {:#b}\n", 255);   // 0b11111111
    fmt::print("  浮点数:  {:.4f}\n", 3.14159265);
    fmt::print("  科学计数: {:e}\n",  12345.678);
    fmt::print("  宽度对齐: {:>10d}\n", 42);    // 右对齐，宽度10
    fmt::print("  填充零:  {:08d}\n",  42);    // 用0填充到8位

    // ----------------------------------------------------------
    // 3. 字符串对齐
    // ----------------------------------------------------------
    fmt::print("\n[字符串对齐]\n");
    fmt::print("  左对齐: |{:<15}|\n", "hello");
    fmt::print("  右对齐: |{:>15}|\n", "hello");
    fmt::print("  居中:   |{:^15}|\n", "hello");
    fmt::print("  填充:   |{:*^15}|\n", "hello");  // 用 * 填充

    // ----------------------------------------------------------
    // 4. 格式化容器（fmt::ranges）
    // ----------------------------------------------------------
    fmt::print("\n[容器格式化]\n");
    std::vector<int> nums = {1, 2, 3, 4, 5};
    fmt::print("  vector: {}\n", nums);

    std::vector<std::string> words = {"foo", "bar", "baz"};
    fmt::print("  strings: {}\n", words);

    // ----------------------------------------------------------
    // 5. fmt::format 返回 string（不直接打印）
    // ----------------------------------------------------------
    fmt::print("\n[format 返回 string]\n");
    std::string msg = fmt::format("圆面积（r={}）= {:.2f}", 5, M_PI * 25);
    fmt::print("  {}\n", msg);

    // ----------------------------------------------------------
    // 6. 彩色终端输出（fmt::color）
    // ----------------------------------------------------------
    fmt::print("\n[彩色输出]\n");
    fmt::print(fmt::fg(fmt::color::green),  "  ✅ 成功信息（绿色）\n");
    fmt::print(fmt::fg(fmt::color::red),    "  ❌ 错误信息（红色）\n");
    fmt::print(fmt::fg(fmt::color::yellow), "  ⚠️  警告信息（黄色）\n");
    fmt::print(fmt::fg(fmt::color::cyan) | fmt::emphasis::bold,
               "  ℹ️  加粗青色文字\n");

    // ----------------------------------------------------------
    // 7. 实际应用：格式化表格
    // ----------------------------------------------------------
    fmt::print("\n[实际应用：打印表格]\n");
    fmt::print("  {:<10} {:>8} {:>10}\n", "Name", "Score", "Grade");
    fmt::print("  {:-<10} {:->8} {:->10}\n", "", "", "");

    struct Student { std::string name; int score; char grade; };
    std::vector<Student> students = {
        {"Alice", 95, 'A'},
        {"Bob",   82, 'B'},
        {"Carol", 76, 'C'},
    };
    for (const auto& s : students) {
        fmt::print("  {:<10} {:>8} {:>10c}\n", s.name, s.score, s.grade);
    }

    return 0;
}

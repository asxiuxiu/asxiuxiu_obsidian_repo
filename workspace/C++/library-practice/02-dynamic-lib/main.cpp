// ============================================================
// 使用动态库 libmystr.dylib / libmystr.so 的示例程序
//
// 编译方式（手动）：
//   cd 02-dynamic-lib
//   g++ -std=c++17 -fPIC -c src/mystr.cpp -Iinclude -o build/mystr.o
//   g++ -shared build/mystr.o -o build/libmystr.dylib    # macOS
//   g++ -shared build/mystr.o -o build/libmystr.so       # Linux
//   g++ -std=c++17 main.cpp -Iinclude -Lbuild -lmystr -o build/demo
//
//   macOS 运行：
//     DYLD_LIBRARY_PATH=build ./build/demo
//   Linux 运行：
//     LD_LIBRARY_PATH=build ./build/demo
//
// 或者直接 make dynamic（会自动处理平台差异）
// ============================================================

#include <iostream>
#include "mystr.h"

// 打印 vector 的小工具
void print_vec(const std::vector<std::string>& v) {
    for (size_t i = 0; i < v.size(); ++i) {
        std::cout << "  [" << i << "] \"" << v[i] << "\"\n";
    }
}

int main() {
    std::cout << "========== libmystr 动态库演示 ==========\n\n";

    // --- split ---
    std::cout << "[split] \"one,two,three\" by ',':\n";
    print_vec(mystr::split("one,two,three", ','));

    // --- trim ---
    std::cout << "\n[trim]  \"  hello world  \" → \""
              << mystr::trim("  hello world  ") << "\"\n";

    // --- to_upper / to_lower ---
    std::cout << "\n[case]  to_upper(\"Hello World\") = \""
              << mystr::to_upper("Hello World") << "\"\n";
    std::cout << "[case]  to_lower(\"Hello World\") = \""
              << mystr::to_lower("Hello World") << "\"\n";

    // --- starts_with / ends_with ---
    std::cout << "\n[prefix] \"libmystr.so\" starts with \"lib\"? "
              << (mystr::starts_with("libmystr.so", "lib") ? "yes" : "no") << "\n";
    std::cout << "[suffix] \"libmystr.so\" ends with \".so\"?   "
              << (mystr::ends_with("libmystr.so", ".so") ? "yes" : "no") << "\n";

    // --- replace_all ---
    std::cout << "\n[replace] replace_all(\"foo bar foo\", \"foo\", \"baz\") = \""
              << mystr::replace_all("foo bar foo", "foo", "baz") << "\"\n";

    // --- repeat ---
    std::cout << "\n[repeat] repeat(\"ha\", 5) = \""
              << mystr::repeat("ha", 5) << "\"\n";

    return 0;
}

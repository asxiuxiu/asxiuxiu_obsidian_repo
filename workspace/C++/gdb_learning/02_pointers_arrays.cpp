/****************************************************************************
 * 02_pointers_arrays.cpp - 指针和数组调试练习
 *
 * 练习目标：
 * 1. 查看数组内容
 * 2. 理解指针和内存地址
 * 3. 使用 display 自动显示变量
 *
 * 调试命令：
 *   g++ -g -O0 02_pointers_arrays.cpp -o 02_pointers_arrays
 *   gdb ./02_pointers_arrays
 *
 *   (gdb) print *array@10     # 打印数组前10个元素
 *   (gdb) print ptr           # 打印指针地址
 *   (gdb) print *ptr          # 打印指针指向的值
 *   (gdb) display arr[i]      # 每次暂停时显示 arr[i]
 ***************************************************************************/

#include <iostream>
#include <cstring>

void print_array(int* arr, int size) {
    std::cout << "数组内容: ";
    for (int i = 0; i < size; ++i) {
        std::cout << arr[i] << " ";
    }
    std::cout << std::endl;
}

void pointer_demo() {
    std::cout << "\n=== 指针基础演示 ===" << std::endl;

    int value = 42;
    int* ptr = &value;

    std::cout << "value = " << value << std::endl;
    std::cout << "ptr = " << ptr << std::endl;      // 地址
    std::cout << "*ptr = " << *ptr << std::endl;    // 解引用

    *ptr = 100;  // 通过指针修改值
    std::cout << "修改后 value = " << value << std::endl;
}

void array_demo() {
    std::cout << "\n=== 数组调试演示 ===" << std::endl;

    int arr[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

    // 在 GDB 中尝试:
    // print arr              # 打印数组首地址
    // print *arr@10          # 打印全部10个元素
    // print arr[0]@5          # 打印前5个元素
    // x/10dw arr              # 以十进制格式查看内存

    std::cout << "数组首元素: " << arr[0] << std::endl;
    std::cout << "数组地址: " << arr << std::endl;

    // 修改数组元素
    for (int i = 0; i < 10; ++i) {
        arr[i] *= 2;  // 每个元素乘以2
        // 在这里可以: display arr[i] 来观察变化
    }

    print_array(arr, 10);
}

void string_demo() {
    std::cout << "\n=== 字符串调试演示 ===" << std::endl;

    char str1[] = "Hello";
    const char* str2 = "World";
    std::string str3 = "GDB Practice";

    // GDB 查看字符串:
    // print str1              # 打印字符串
    // print str2              # 打印指针指向的字符串
    // print str3.c_str()      # 打印 std::string 内容
    // x/s str1                # 使用 examine 命令

    std::cout << "str1: " << str1 << std::endl;
    std::cout << "str2: " << str2 << std::endl;
    std::cout << "str3: " << str3 << std::endl;

    // 字符串操作
    strcat(str1, " GDB");
    std::cout << "连接后: " << str1 << std::endl;
}

void dynamic_memory_demo() {
    std::cout << "\n=== 动态内存调试 ===" << std::endl;

    int* dynamic_arr = new int[5]{10, 20, 30, 40, 50};

    // GDB 调试动态内存:
    // print *dynamic_arr@5     # 查看动态分配的数组
    // info locals              # 查看局部变量

    std::cout << "动态数组: ";
    for (int i = 0; i < 5; ++i) {
        std::cout << dynamic_arr[i] << " ";
    }
    std::cout << std::endl;

    delete[] dynamic_arr;
}

int main() {
    std::cout << "=== 指针和数组 GDB 调试练习 ===" << std::endl;

    pointer_demo();
    array_demo();
    string_demo();
    dynamic_memory_demo();

    std::cout << "\n程序正常结束" << std::endl;
    return 0;
}

#include <iostream>

// 主模板
template<typename T, size_t N>
struct Data {
    T values[N];
    void print() { std::cout << "主模板print\n"; }
};

// 偏特化 - 没有print方法
template<size_t N>
struct Data<bool, N> {
    unsigned char values[(N + 7) / 8];
};

int main() {
    Data<bool, 8> d2;
    d2.print();  // 编译错误！偏特化没有print
    
    return 0;
}

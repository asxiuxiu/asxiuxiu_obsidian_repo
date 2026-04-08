#include <iostream>

// 主模板
template<typename T, size_t N>
struct Data {
    T values[N];
    void print() { std::cout << "主模板print\n"; }
};

// 偏特化
template<size_t N>
struct Data<bool, N> {
    unsigned char values[(N + 7) / 8];
    // 不写print方法！
};

int main() {
    Data<int, 5> d1;
    d1.print();  // 主模板，有print
    
    Data<bool, 8> d2;
    // d2.print();  // 偏特化没有print，编译错误！
    
    return 0;
}

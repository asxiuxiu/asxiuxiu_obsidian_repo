#include <iostream>

template<typename T>
T myGcd(T a, T b)
{
    while (b != T(0))
    {
        T r = a % b;
        a = b;
        b = r;
    }
    return a;
}


template< typename C>
void foo(const C& c)
{
    using std::begin;
    using std::end;
    for (auto it = begin(c); it != end(c); ++it)
    {
        std::cout << *it << std::endl;
    }

}

template<typename T, size_t N>
struct Data
{
    T values[N];
};

template<>
struct Data<bool, 2>
{
    unsigned char values[1];
};


template<size_t N>
struct Data<bool, N>
{
    unsigned char values[(N + 7) / 8];
};



int main() {
    float ret = myGcd<float>(1.5, 2.5);
    std::cout << "GCD of 1.5 and 2.5 is: " << ret << std::endl;
    return 0;
}

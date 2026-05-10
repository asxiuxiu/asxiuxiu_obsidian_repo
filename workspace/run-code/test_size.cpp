#include <iostream>

class NoVirtual {
    int x;
};

class WithVirtual {
    int x;
public:
    virtual void foo() {}
};

int main() {
    std::cout << "sizeof(NoVirtual)   = " << sizeof(NoVirtual) << "\n";
    std::cout << "sizeof(WithVirtual) = " << sizeof(WithVirtual) << "\n";
    return 0;
}

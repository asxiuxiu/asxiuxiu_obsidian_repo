#include <iostream>

class Base {
public:
    Base() { foo(); }
    virtual void foo() { std::cout << "Base::foo\n"; }
};

class Derived : public Base {
public:
    void foo() override { std::cout << "Derived::foo\n"; }
};

int main() {
    Derived d;
    return 0;
}

#include <iostream>

class Base {
public:
    virtual void foo() = 0;
};

void Base::foo() {
    std::cout << "Base::foo (default)\n";
}

class Derived : public Base {
public:
    void foo() override {
        Base::foo();
        std::cout << "Derived::foo\n";
    }
};

int main() {
    Derived d;
    d.foo();
    return 0;
}

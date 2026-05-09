// flags: -O0 -g
#include <iostream>

class PlainPerson {
    int age;
};

class VirtualPerson {
public:
    virtual void sayHello() {}
    int age;
};

int main() {
    std::cout << "sizeof(PlainPerson)   = " << sizeof(PlainPerson) << "\n";
    std::cout << "sizeof(VirtualPerson) = " << sizeof(VirtualPerson) << "\n";
    return 0;
}
/****************************************************************************
 * 03_classes_objects.cpp - 类和对象调试练习
 *
 * 练习目标：
 * 1. 调试类成员函数
 * 2. 查看对象状态
 * 3. 理解 this 指针
 *
 * 调试命令：
 *   g++ -g -O0 03_classes_objects.cpp -o 03_classes_objects
 *   gdb ./03_classes_objects
 *
 *   (gdb) break Person::setAge     # 在类方法上设置断点
 *   (gdb) print person             # 打印对象内容
 *   (gdb) print this->name         # 查看当前对象的成员
 *   (gdb) info locals              # 查看局部变量
 ***************************************************************************/

#include <iostream>
#include <string>
#include <vector>

class Person {
private:
    std::string name;
    int age;
    static int person_count;  // 静态成员

public:
    Person(const std::string& n, int a) : name(n), age(a) {
        ++person_count;
        std::cout << "Person created: " << name << std::endl;
    }

    ~Person() {
        --person_count;
        std::cout << "Person destroyed: " << name << std::endl;
    }

    void setAge(int a) {
        // 在这里设置断点，观察 this 指针
        if (a >= 0 && a <= 150) {
            age = a;
        } else {
            std::cout << "Invalid age: " << a << std::endl;
        }
    }

    int getAge() const {
        return age;
    }

    std::string getName() const {
        return name;
    }

    void display() const {
        std::cout << "Name: " << name << ", Age: " << age << std::endl;
    }

    static int getCount() {
        return person_count;
    }
};

int Person::person_count = 0;

class BankAccount {
private:
    std::string account_number;
    double balance;

public:
    BankAccount(const std::string& num, double initial_balance)
        : account_number(num), balance(initial_balance) {
        std::cout << "Account " << account_number << " created with balance: " << balance << std::endl;
    }

    void deposit(double amount) {
        if (amount > 0) {
            balance += amount;
            std::cout << "Deposited " << amount << ", new balance: " << balance << std::endl;
        }
    }

    bool withdraw(double amount) {
        if (amount > 0 && amount <= balance) {
            balance -= amount;
            std::cout << "Withdrew " << amount << ", new balance: " << balance << std::endl;
            return true;
        }
        std::cout << "Withdrawal failed!" << std::endl;
        return false;
    }

    double getBalance() const {
        return balance;
    }

    void printInfo() const {
        std::cout << "Account: " << account_number << ", Balance: $" << balance << std::endl;
    }
};

void person_demo() {
    std::cout << "\n=== Person 类调试 ===" << std::endl;

    Person alice("Alice", 25);
    Person bob("Bob", 30);

    // 在这里可以使用: print alice 查看对象内容
    alice.display();
    bob.display();

    alice.setAge(26);  // 在这里设置断点调试 setAge
    std::cout << "After birthday, Alice is " << alice.getAge() << std::endl;

    std::cout << "Total persons: " << Person::getCount() << std::endl;
}

void bank_demo() {
    std::cout << "\n=== BankAccount 类调试 ===" << std::endl;

    BankAccount account("123456789", 1000.0);

    // 调试点：观察对象内部状态变化
    account.printInfo();

    account.deposit(500.0);
    account.withdraw(200.0);

    // 尝试超额取款
    account.withdraw(2000.0);

    account.printInfo();
}

void vector_demo() {
    std::cout << "\n=== STL Vector 调试 ===" << std::endl;

    std::vector<Person> people;

    people.emplace_back("Charlie", 22);
    people.emplace_back("David", 28);
    people.emplace_back("Eve", 35);

    // GDB 打印 STL 容器:
    // print people                      # 查看容器信息
    // print people.size()               # 查看大小
    // print people[0]                   # 查看第一个元素

    std::cout << "People count: " << people.size() << std::endl;

    for (const auto& person : people) {
        person.display();
    }
}

int main() {
    std::cout << "=== 类和对象 GDB 调试练习 ===" << std::endl;

    person_demo();
    bank_demo();
    vector_demo();

    std::cout << "\n程序正常结束" << std::endl;
    return 0;
}

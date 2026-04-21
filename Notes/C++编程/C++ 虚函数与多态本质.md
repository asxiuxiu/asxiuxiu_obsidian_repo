---
title: C++ 虚函数与多态本质
date: 2026-04-19
tags:
  - C++
  - 多态
  - 虚函数
  - 内存布局
  - 性能优化
aliases:
  - 虚函数表原理
  - vtable 与 vptr
---

> [[索引|← 返回 C++编程索引]]

# C++ 虚函数与多态本质

---

## Why：为什么要深入理解虚函数？

- **性能优化的根基**：游戏引擎中 90% 的底层优化决策（Cache 友好、分支预测、SIMD）都建立在理解代码如何被 CPU 执行的基础上。虚函数是 OOP 与高性能 DOD 之间的核心分歧点。
- **面试必考，但多数人只背结论**："虚函数通过虚函数表实现"这句话人人会说，但 vtable 放在哪？vptr 谁写入？构造期间调用虚函数会怎样？能答清楚的人很少。
- **理解 ECS 为什么取代 OOP 的关键**：虚函数表跳转本质上是不可预测的分支（目标地址运行时才能确定），同时伴随指针追逐（Pointer Chasing）。这是现代引擎转向数据导向设计（DOD）和 ECS 架构的根本动力之一。

---

## What：虚函数是什么？

### 静态绑定 vs 动态绑定

C++ 的函数调用有两种绑定方式：

| 绑定方式 | 决定时机 | 典型场景 | 性能 |
|---------|---------|---------|------|
| **静态绑定** | 编译期 | 普通函数调用、非虚成员函数、对象直接调用虚函数 | 直接 `call` 固定地址，最快 |
| **动态绑定** | 运行期 | 通过指针/引用调用虚函数 | 读 vptr → 读 vtable → 间接 `call`，有额外开销 |

```cpp
class Base {
public:
    void normal();          // 普通函数
    virtual void virt();    // 虚函数
};

Base b;
Base* p = &b;

b.normal();   // 静态绑定：编译器直接生成 call Base::normal
b.virt();     // 静态绑定！对象直接调用，不走 vtable
p->virt();    // 动态绑定：运行时查 vtable 决定调用哪个版本
```

> [!warning] 常见误区
> `b.virt()` 虽然是虚函数，但**通过对象调用时仍然是静态绑定**。只有通过**指针或引用**调用时才走动态绑定。

### vtable（虚函数表）：编译器生成，只读共享

编译器为每个**包含虚函数的类**生成一张 vtable。它本质上是一个**函数指针数组**：

```cpp
class Base {
    virtual void foo();
    virtual void bar();
};
class Derived : public Base {
    void foo() override;  // 覆盖
    // bar 继承 Base 的实现
};
```

内存中的 vtable 布局：

```
Base::vtable      →  [ &Base::foo , &Base::bar  ]
                       偏移0        偏移8

Derived::vtable   →  [ &Derived::foo , &Base::bar  ]
                       偏移0          偏移8
```

| 特性 | 说明 |
|------|------|
| 谁生成 | 编译器，每个含虚函数的类一张 |
| 放哪里 | 只读数据段（`.rodata` / `.rdata`），运行时不可修改 |
| 共享方式 | **全类共享**，100 个 `Derived` 对象只有一张 `Derived::vtable` |
| 内容 | 函数指针数组，通常还包含 RTTI 信息（`type_info` 指针）和虚析构函数槽位 |

### vptr（虚表指针）：对象的一部分，构造函数写入

**每个包含虚函数的对象实例**，内存布局的最开头（通常偏移 0）隐藏着一个指针：

```
Base 对象 b：
┌─────────┬────────────┐
│  vptr   │  Base数据  │
│ (8字节) │            │
└────┬────┴────────────┘
     │
     ▼
Base::vtable [ &Base::foo, &Base::bar ]

Derived 对象 d：
┌─────────┬────────────┬──────────────┐
│  vptr   │  Base数据  │ Derived数据  │
│ (8字节) │            │              │
└────┬────┴────────────┴──────────────┘
     │
     ▼
Derived::vtable [ &Derived::foo, &Base::bar ]
```

#### 构造函数中的 vptr 设置

vptr 不是魔法自动出现的，而是**编译器在构造函数中插入代码写入的**。更关键的是：**vptr 在构造过程中会变化**。

```cpp
Derived::Derived() : Base() {
    // [编译器插入] 进入 Base() 前：this->__vptr = &Base::vtable;
    // 执行 Base::Base() ...
    // [编译器插入] 离开 Base() 后：this->__vptr = &Derived::vtable;
    // 执行 Derived 构造体 ...
}
```

> [!info] 为什么构造期间要改 vptr？
> 如果在 `Base::Base()` 中调用了虚函数，此时对象还不是完整的 `Derived`。vptr 指向 `Base::vtable` 可以保证**调用的是 Base 版本**，避免访问到未初始化的 Derived 成员。这是 C++ 的安全规则：构造/析构期间，虚函数不下降到派生类。

#### 析构时的逆向过程

```cpp
Derived::~Derived() {
    // 执行 Derived 析构体 ...
    // [编译器插入] 离开前：this->__vptr = &Base::vtable;
    // 执行 Base::~Base() ...
}
```

析构后将 vptr 改回基类版本，防止在基类析构体中通过虚函数调用已销毁的派生类实现。

---

## How：从代码到 CPU 的完整链路

### 1. 编译器如何区分虚函数与普通函数？

编译器在**语义分析阶段**就能判断：

- `p->foo()` 中 `foo` 是 `virtual` → 生成"读 vptr → 读 vtable → 间接 call"序列
- `p->bar()` 中 `bar` 非 `virtual` → 直接 `call Base::bar`

### 2. 汇编层面的区别

**普通函数调用（Direct Call）**：

```asm
call 0x401240        ; E8 xx xx xx xx，目标地址硬编码在指令中
```

**虚函数调用（Indirect Call）**：

```asm
mov  rax, [rbx]      ; 从对象首地址读取 vptr
mov  rax, [rax+0x8]  ; 从 vtable 读取第 N 个槽位（编译期已知偏移）
call rax             ; FF D0，目标地址在寄存器中，运行时确定
```

### 3. CPU 分支预测器视角：为什么是不可预测的分支？

CPU 的分支预测器不认识"C++ 多态"，它只看指令 opcode：

| 指令类型 | x86 编码 | 预测器看到的 |
|---------|---------|------------|
| `call 0x401240` | `E8 xx xx xx xx` | **直接分支**：目标地址在指令里，取指阶段即知 |
| `call rax` | `FF D0` | **间接分支**：目标地址在寄存器里，必须等前面指令执行完 |

对于间接分支，预测器会查 **Indirect Branch Predictor**（如 ITB、TAGE）来猜 `rax` 里可能是哪个地址。但如果同一个调用点交替处理不同类型对象：

```cpp
for (auto* obj : mixed_list) {  // Base、DerivedA、DerivedB 混杂
    obj->virtualFunc();
}
```

目标地址序列对预测器来说是**随机的**，命中率暴跌。一旦猜错：

- 流水线中预取的错误路径指令全部作废（Flush）
- 重新从正确地址取指填充
- 代价 **10~20+ 个周期**

同时，虚函数调用还伴随**指针追逐**：

```
对象内存 → vptr（L1 缓存未命中？）→ vtable（可能跨缓存行）→ 函数地址 → 函数代码
```

每一步都可能是缓存未命中。

### 4. 多继承下的复杂布局（了解即可）

多继承时，对象包含**多个 vptr**（每个基类子对象一个）：

```cpp
class Base1 { virtual void f1(); };
class Base2 { virtual void f2(); };
class Derived : public Base1, public Base2 { ... };
```

```
Derived 内存：
┌─────────┬──────────┬─────────┬──────────┐
│ vptr1   │ Base1数据 │ vptr2   │ Base2数据 │
│(Base1   │          │(Base2   │          │
│ vtable) │          │ vtable) │          │
└─────────┴──────────┴─────────┴──────────┘
```

`this` 指针在基类间转换时可能需要调整，这也是 `dynamic_cast` 和 `static_cast` 在多继承时行为不同的底层原因。

---

## 构造与析构的陷阱

### 构造期间能调用虚函数吗？

**语法上可以，但语义上不会下降到派生类。**

```cpp
class Base {
public:
    Base() { virt(); }   // 调用的是 Base::virt，不是 Derived::virt！
    virtual void virt() { cout << "Base\n"; }
};

class Derived : public Base {
public:
    void virt() override { cout << "Derived\n"; }
};

Derived d;  // 输出 "Base"，不是 "Derived"
```

原因：进入 `Base::Base()` 时，vptr 指向 `Base::vtable`。此时即使对象将来会是 `Derived`，虚函数解析也只会查到 `Base` 的版本。

### 析构函数必须是虚函数吗？

**如果类作为基类使用，必须是。**

```cpp
class Base {
public:
    ~Base() {}  // 非虚析构！
};

class Derived : public Base {
public:
    ~Derived() { /* 释放资源 */ }
};

Base* p = new Derived();
delete p;  // 只调用 Base::~Base()！Derived 的资源泄漏了！
```

`delete p` 时，如果 `~Base()` 不是虚函数，编译器直接静态绑定到 `Base::~Base()`，**不会查 vtable**，导致 `Derived::~Derived()` 永远不会执行。

> [!danger] 设计红线
> 任何打算被继承的类，析构函数都应该是 `virtual`（或用 `= default` 让编译器生成虚析构）。否则通过基类指针 `delete` 派生类对象时，行为未定义（通常表现为资源泄漏）。

---

## 优化与替代方案

虚函数的性能开销（分支预测失败 + 指针追逐 + 指令 Cache 未命中）在现代引擎中不可忽视。常见的优化/替代方向：

| 方案                                | 原理                                   | 适用场景                     |
| --------------------------------- | ------------------------------------ | ------------------------ |
| **Type Splitting**                | 按类型分数组，用 `switch` 替代虚函数              | ECS 中同类型对象批量处理           |
| **CRTP（奇异递归模板模式）**                | 用模板静态多态替代动态多态，编译期确定调用目标              | 需要多态语义但追求零开销             |
| **`std::variant` + `std::visit`** | 类型安全的联合体，访问时编译期生成分发表                 | 有限类型集合的替代多态              |
| **函数指针内联缓存**                      | 运行时缓存上次调用的目标地址                       | JavaScript V8 等动态语言的优化技巧 |
| **Final 关键字**                     | `final class` / `final` 虚函数允许编译器去虚拟化 | 确定不会被覆盖的类/函数             |

> [!tip] 编译器去虚拟化（Devirtualization）
> 如果编译器能静态证明 `p` 实际指向的类型（如 `Base* p = new Derived(); p->foo();` 且 `foo` 是 `final`），现代编译器（GCC/Clang/MSVC）会直接生成静态绑定的 `call`，跳过 vtable 查找。

### Type Splitting：按类型分片替代虚函数

#### Why：为什么批量处理时虚函数性能差？

虚函数调用是**间接跳转**（`call rax`），目标地址运行时才能确定。当循环里混杂多种类型对象时，分支预测器看到的地址序列近乎随机，预测失败代价高达 10~20+ 周期。同时伴随指针追逐（vptr → vtable → 函数代码），每一步都可能 Cache Miss。

#### What：类型分片是什么？

**把不同类型的对象拆到不同的数组里**，同类型的数据紧挨着放。处理时逐个数组遍历，类型在循环外就已经确定。

类型分片是 ECS（Entity-Component-System）架构的**数据层基础**，但两者不等同：

| 概念 | 类型分片 | ECS |
|------|---------|-----|
| **本质** | 一种**数据布局策略** | 一种**架构模式** |
| **组成** | 按类型拆分数组 | ID（Entity）+ 纯数据（Component）+ 批量逻辑（System）|
| **关系** | ECS 的 Component 存储天然就是类型分片 | 在类型分片之上绑定了"无行为的数据"契约 |

#### How：代码对比与底层原理

**传统虚函数方式（分支预测不友好）：**

```cpp
std::vector<Entity*> entities;  // 各种类型混杂

for (auto* e : entities) {
    e->update();  // 虚函数调用：目标地址随机，分支预测器猜不准
}
```

**类型分片方式（Cache + 分支预测友好）：**

```cpp
std::vector<Enemy> enemies;
std::vector<Bullet> bullets;
std::vector<Particle> particles;

for (auto& e : enemies) {
    updateEnemy(e);   // 直接调用，编译期确定地址，无分支预测风险
}

for (auto& b : bullets) {
    updateBullet(b);
}
```

如果必须在同一循环里处理多种类型，用 `switch` 或显式类型标记替代虚函数：

```cpp
enum class EntityType { Enemy, Bullet, Particle };

for (auto& e : entities) {
    switch (e.type) {  // 直接条件分支，比间接跳转更好预测
        case EntityType::Enemy:   updateEnemy(e); break;
        case EntityType::Bullet:  updateBullet(e); break;
    }
}
```

#### `switch` / `if-else` vs 虚函数：分支预测的本质区别

| 特性 | `switch` / `if-else` | 虚函数调用 |
|------|---------------------|-----------|
| 指令类型 | **直接条件分支**（`jz label`） | **间接跳转**（`call rax`） |
| 目标地址 | 指令中硬编码相对偏移 | 藏在寄存器里，运行时计算 |
| 分支预测器工作 | 猜"跳还是不跳"（二元问题） | 猜"rax 里是哪个地址"（多选问题） |
| 预测失败惩罚 | ~10-20 周期 | ~10-20+ 周期 |

> **核心结论**：`switch` 和 `if-else` 在分支预测层面**半斤八两**，它们**共同的优势**是"直接分支 vs 虚函数的间接跳转"。真正的性能分水岭是"跳转类型"而非"switch 还是 if"。

#### 编译器如何优化 `switch`：跳转表与 case 密集

编译器对 `switch` 的优化取决于 case 值的**数值跨度**，而非 case 的个数：

| case 值        | 数值跨度                | 跳转表大小                 | 编译器策略                | 分支预测影响                |
| ------------- | ------------------- | --------------------- | -------------------- | --------------------- |
| `0, 1, 2, 3`  | `3 - 0 + 1 = 4`     | 4 × 8 = **32 字节**     | **跳转表（Jump Table）**  | **无分支预测问题**，数组索引直接算地址 |
| `0, 100, 200` | `200 - 0 + 1 = 201` | 201 × 8 = **1608 字节** | 退化为 `if-else` 链或二分查找 | 和普通条件分支一样，预测器需要猜      |

**跳转表的本质**：一个紧凑的地址数组，用变量值当索引直接查表跳转。整个过程没有条件跳转指令。

```asm
; 跳转表形态（case 0,1,2,3）
cmp     eax, 3          ; 越界检查
ja      .L_default
lea     rdx, [rip + .L_jump_table]
movsxd  rax, [rdx + rax*4]  ; 用值当索引取目标偏移
add     rax, rdx
jmp     rax             ; 直接跳转，无需分支预测

.L_jump_table:
    .long .L_case_0 - .L_jump_table
    .long .L_case_1 - .L_jump_table
    .long .L_case_2 - .L_jump_table
    .long .L_case_3 - .L_jump_table
```

在 ECS 中，实体类型通常用从 0 开始连续编号的枚举（如 `enum class Type : uint8_t { Enemy, Bullet, Particle }`），这种**天然密集**的 case 值使 `switch(type)` 极易被编译为跳转表——这也是它替代虚函数时性能好的原因之一。

---

## 关键数字速查

| 指标 | 数量级 | 含义 |
|------|--------|------|
| 寄存器访问 | ~1 周期 | CPU 内部最快 |
| L1 缓存命中 | ~3-5 周期 | vptr 通常在此层级 |
| 分支预测失败 | ~10-20 周期 | 虚函数目标随机时的惩罚 |
| L2 缓存命中 | ~10-15 周期 | vtable 可能在此层级 |
| 主内存访问 | ~100-300 周期 | 最坏情况下的指针追逐代价 |

---

## 延伸阅读

- [[计算机体系结构速览：CPU、内存与总线]] — 分支预测器、流水线、Cache 层次结构
- [[最小ECS数据层]] — 用 Type Splitting 替代虚函数的实践
- [[Notes/C++编程/C++ 对象生存期与 RAII]] — 构造/析构顺序与资源管理
- [[Notes/C++编程/C++ 值类别与移动语义]] — 对象生命周期的高级话题

---

> [!quote] 一句话总结
> 虚函数 = 编译器为每个类生成一张只读函数表（vtable），每个对象藏一个指针（vptr）指向它。动态绑定的代价是：运行时多两次内存读取 + 一次间接跳转 + 分支预测器失明。

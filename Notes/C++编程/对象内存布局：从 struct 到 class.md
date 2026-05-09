---
title: 对象内存布局：从 struct 到 class
date: 2026-05-09
tags:
  - C++
  - 内存布局
  - 对齐
  - 对象模型
aliases:
  - struct 内存布局
  - class 内存布局
  - 成员对齐与填充
---

> [[Notes/C++编程/索引|← 返回 C++ 编程索引]]

# 对象内存布局：从 struct 到 class

> [!info] 一句话概括
> C++ 对象的内存布局并非「成员紧挨着放」那么简单——编译器为了硬件效率会插入不可见的填充字节，而 class 与 struct 的差异、成员函数与虚函数的存在，都会以特定方式改变对象在内存中的真实大小与形态。

---

## 问题 0：struct 的成员在内存里就是按声明顺序紧挨着放的吗？

想象你在写一个多人游戏的网络同步模块。服务端传来一段二进制数据，你打算用 C 结构体直接 `memcpy` 进去读取：

```cpp
// flags: -O0 -g
#include <iostream>
#include <cstring>

struct PlayerData {
    char  id;      // 玩家 ID，1 字节
    int   score;   // 分数，4 字节
    char  flag;    // 状态标记，1 字节
};

int main() {
    // 服务端发来的 6 字节原始数据：id + score + flag
    unsigned char raw[6] = { 0x01, 0x00, 0x00, 0x00, 0x64, 0x00 };

    PlayerData p;
    std::memcpy(&p, raw, 6);

    std::cout << "sizeof(PlayerData) = " << sizeof(PlayerData) << "\n";
    std::cout << "expected (1+4+1)   = " << (1 + 4 + 1) << "\n";
    std::cout << "score = " << p.score << "\n";
    return 0;
}
```

如果你运行这段代码，大概率会得到 `sizeof(PlayerData) = 12`（或 8，取决于平台和编译器），而不是你预期的 6。更糟的是，`p.score` 读出来的值可能完全错误——因为 `memcpy` 把 6 字节原样塞进了编译器实际分配了 12 字节的结构体里，成员之间的「空隙」被服务端数据错位填充了。

这就是最朴素的认知陷阱：**以为 `sizeof` 等于各成员大小之和**。

那么，多出来的字节是哪来的？为什么编译器不按照我们写的顺序「紧凑排列」？

---

## 问题 1：编译器为什么要在成员之间塞 padding？

### 硬件层面的代价

现代 CPU 不是逐字节读写内存的。数据总线通常以 4 字节、8 字节甚至 16 字节为一个「块」来搬运数据。如果一个 4 字节的 `int` 刚好落在某个块的起始位置，CPU 一次就能读完；但如果它跨越了两个块的边界，CPU 就要做两次总线访问，再把两半拼起来——这就是**未对齐访问的惩罚**。

在某些架构上（比如早期的 ARM），未对齐访问甚至直接触发硬件异常，程序崩溃。x86-64 虽然能容忍未对齐访问，但性能会显著下降。所以 C++ 编译器默认会遵循一条规则：**让每个成员的起始地址是它自身大小的整数倍**。

### 对齐规则的具体表现

回到上面的 `PlayerData`：

```cpp
struct PlayerData {
    char  id;      // 偏移 0，占 1 字节
    // 编译器插入 3 字节 padding，让下一个成员对齐到 4 的倍数
    int   score;   // 偏移 4，占 4 字节
    char  flag;    // 偏移 8，占 1 字节
    // 编译器再插入 3 字节 padding，让整个结构体大小是其最大成员（int = 4）的倍数
};
```

所以实际布局是：`[id][pad×3][score][flag][pad×3]`，总大小 12 字节。

你可以用 `offsetof` 验证每个成员的真实偏移：

```cpp
// flags: -O0 -g
#include <iostream>
#include <cstddef>

struct PlayerData {
    char  id;
    int   score;
    char  flag;
};

int main() {
    std::cout << "offset of id    = " << offsetof(PlayerData, id) << "\n";
    std::cout << "offset of score = " << offsetof(PlayerData, score) << "\n";
    std::cout << "offset of flag  = " << offsetof(PlayerData, flag) << "\n";
    std::cout << "sizeof(PlayerData) = " << sizeof(PlayerData) << "\n";
    return 0;
}
```

### 成员顺序直接影响大小

对齐不仅造成 padding，**成员声明顺序还会直接影响结构体总大小**。考虑这两个结构体：

```cpp
// flags: -O0 -g
#include <iostream>

struct BadOrder {
    char  a;    // 1 + 7 pad
    double b;   // 8
    char  c;    // 1 + 7 pad
};              // 总计 24

struct GoodOrder {
    double b;   // 8
    char  a;    // 1
    char  c;    // 1 + 6 pad
};              // 总计 16

int main() {
    std::cout << "sizeof(BadOrder)  = " << sizeof(BadOrder) << "\n";
    std::cout << "sizeof(GoodOrder) = " << sizeof(GoodOrder) << "\n";
    return 0;
}
```

仅仅是把最大的成员放在最前面，就能让 `GoodOrder` 比 `BadOrder` 少 8 字节。在引擎中，如果一个场景里有十万个这样的对象，内存占用差距就是近 1MB。

但引擎开发中还有一种相反的需求：有时候我们**故意不想对齐**。比如读取一个固定格式的文件头、解析网络协议包，或者把数据直接传给 GPU——这些场景下，数据结构的大小和布局必须严格符合外部约定，编译器不能自作主张塞 padding。这就引出了下一个问题。

---

## 问题 2：我能控制这些 padding 吗？什么时候该控制，代价是什么？

### 强制紧凑布局

C++ 提供了多种方式告诉编译器「别塞 padding」：

```cpp
// flags: -O0 -g
#include <iostream>

// MSVC/GCC/Clang 都支持的 pack 指令
#pragma pack(push, 1)
struct PackedPlayer {
    char  id;
    int   score;
    char  flag;
};
#pragma pack(pop)

struct NormalPlayer {
    char  id;
    int   score;
    char  flag;
};

int main() {
    std::cout << "sizeof(NormalPlayer) = " << sizeof(NormalPlayer) << "\n";
    std::cout << "sizeof(PackedPlayer) = " << sizeof(PackedPlayer) << "\n";
    return 0;
}
```

`#pragma pack(push, 1)` 的意思是：从这个位置开始，所有结构体的对齐单位强制设为 1 字节——也就是不对齐。`PackedPlayer` 的大小会变成 6，完全按你写的顺序紧密排列。`pop` 则恢复之前的设置，避免影响后续代码。

GCC/Clang 还提供了属性语法：

```cpp
struct __attribute__((packed)) PackedPlayer {
    char  id;
    int   score;
    char  flag;
};
```

效果与 `#pragma pack(1)` 相同。

### 什么时候该 packed？

- **网络协议 / 文件格式**：协议头规定了每个字段的精确偏移，容不得编译器插入额外字节。
- **GPU 数据上传**：Vertex Buffer、Uniform Block 的布局通常有严格的内存对齐约定，不匹配会导致渲染错误。
- **与 C 代码 / 外部库互操作**：对方按紧凑布局解析数据，你必须保持一致。

### 代价：性能与正确性

`packed` 不是免费午餐。让 `int` 从非 4 字节对齐的地址读取，在 x86-64 上会触发额外的微指令开销，在 ARM 上可能直接崩溃。所以引擎代码中常见的做法是**「两端对齐」**：

- 数据在内存中存储时保持紧凑（packed），节省带宽和缓存；
- 读取后立刻解压到本地变量或对齐的结构体中，再做计算。

此外，C++11 还引入了 `alignas` 用于**增加**对齐要求（而非减少）：

```cpp
struct alignas(16) Vec4 {
    float x, y, z, w;
};
```

这会把 `Vec4` 的对齐要求提升到 16 字节，确保它能直接用于 SSE/AVX SIMD 指令。关于 SIMD 对齐的深入讨论，参见相关笔记。

现在我们已经理解了对齐和填充。但还有一个更基础的问题：C++ 的 `class` 和 C 的 `struct` 在内存上是一回事吗？成员函数、访问控制、虚函数……这些语法层面的差异，会不会偷偷改变对象的内存形态？

---

## 问题 3：C++ class 和 C struct 在内存上真的一样吗？

### 成员函数不占用对象空间

很多人初学 C++ 时会有这样的直觉：类里定义了成员函数，那对象里是不是存着这些函数的「副本」？答案是**不占用**。成员函数的代码被编译成普通机器指令，存放在代码段（text segment）。对象里只存数据成员，调用成员函数时编译器悄悄把对象地址作为隐式的 `this` 指针传进去。

```cpp
// flags: -O0 -g
#include <iostream>

struct PlainStruct {
    int x;
    int y;
};

class PersonClass {
public:
    void sayHello() {}   // 成员函数
    int age;
    int height;
};

int main() {
    std::cout << "sizeof(PlainStruct) = " << sizeof(PlainStruct) << "\n";
    std::cout << "sizeof(PersonClass) = " << sizeof(PersonClass) << "\n";
    return 0;
}
```

你会发现两者大小完全相同（都是 8 字节），成员函数对对象大小零贡献。

### static 成员也不占用对象空间

`static` 成员属于类而不是某个对象，所有实例共享一份，存放在全局/静态数据区：

```cpp
class PersonClass {
public:
    void sayHello() {}
    int age;
    static int count;  // 不占用对象空间
};

int PersonClass::count = 0;
```

### 访问控制不影响布局

`private`、`protected`、`public` 只是编译期的访问权限检查，运行时完全不存在。编译器不会因为访问控制而重新排列成员或插入额外数据。

### 空类的大小为什么不是 0？

```cpp
// flags: -O0 -g
#include <iostream>

struct Empty {};

int main() {
    std::cout << "sizeof(Empty) = " << sizeof(Empty) << "\n";
    return 0;
}
```

空类的大小通常是 1，而不是 0。原因很简单：**C++ 要求每个对象必须有独一无二的内存地址**。如果空类大小为 0，那么 `Empty a, b;` 的 `&a` 和 `&b` 可能指向同一个地址，这会破坏「对象身份」的基本语义。多继承场景中空类可能通过**空基类优化（EBO）**被压缩，但那是更深入的议题。

### 虚函数会改变一切

前面说的「class 和 struct 内存一样」在虚函数面前就不成立了。一旦类声明了虚函数，编译器就会在对象开头（通常）插入一个**虚指针（vptr）**，指向该类的虚函数表（vtable）。

```cpp
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
```

在 64 位系统上，`PlainPerson` 通常是 4 字节，而 `VirtualPerson` 会变成 16 字节（4 字节的 `age` + 8 字节的 vptr + 4 字节尾部填充，对齐到 8）。

这意味着：虚函数不仅引入了间接调用开销，还改变了对象的内存布局——这是 ECS 架构中刻意避免虚函数的核心原因之一。关于 vptr、vtable 和动态派发的完整机制，参见 [[Notes/C++编程/C++ 虚函数与多态本质|虚函数与多态本质]]；多重继承下的多个 vptr 和 this 指针调整，参见 [[Notes/C++编程/多重继承的内存布局与 this 指针调整]]。

---

## 问题 4：在引擎中，这些知识怎么落地？

理解了布局规则后，引擎开发中的很多设计决策就不再是「玄学」：

- **为什么 `Vec3` 经常是 12 字节，但 `Vec4` 要 padding 到 16 字节？** 因为 GPU 的常量缓冲区和 SIMD 寄存器要求 16 字节对齐，12 字节的 `Vec3` 如果直接塞到数组里会导致后续元素错位。
- **为什么改了一个 struct 的成员顺序，网络同步就崩了？** 因为协议解析代码假设了固定的偏移量，而编译器根据新顺序重新计算了 padding。
- **为什么 ECS 的组件数组要禁用虚函数？** 因为 vptr 会让每个组件实例多 8 字节，还会破坏 SoA（Structure of Arrays）布局的紧凑性和 SIMD 友好性。

这些决策的底层支撑，就是本节讨论的内存布局与对齐规则。

---

## 总结

- **对齐是硬件需求，不是编译器任性**。编译器插入 padding 是为了让成员落在其自然对齐边界上，保证访问效率和平台兼容性。
- **成员顺序影响总大小**。把大成员放前面、小成员凑一起，能显著减少内存浪费——这在引擎大规模数据结构中尤为重要。
- **`#pragma pack` 和 `alignas` 是双刃剑**。前者压缩布局以满足外部格式要求，后者提升对齐以适配 SIMD/GPU；两者都意味着你对布局负全责。
- **class 与 struct 的内存差异只体现在「有无虚函数」**。成员函数、static 成员、访问控制都不改变对象大小；空类大小为 1 是为了保证对象身份；虚函数引入 vptr，彻底改写布局。

---

## 引擎映射

| 引擎 | 应用场景 | 具体体现 |
|------|---------|---------|
| **SelfGameEngine** | 数学类型的紧凑与 SIMD 对齐 | `Vec3`（12 字节）用于普通计算，`Vec4`（16 字节，alignas(16)）用于 SIMD/GPU 上传；网络协议结构体使用 `#pragma pack(1)` 保证跨平台一致 |
| **UE** | `UProperty` 的序列化与对齐控制 | UHT 生成的反射代码记录了每个 `UProperty` 的偏移（`Offset_Internal`），该偏移由编译器根据对齐规则计算；`UPROPERTY()` 宏可显式指定对齐要求；`UScriptStruct` 的内存布局必须与 C++ 原生布局严格一致才能被蓝图正确读写 |

> [!note] 关键取舍
> SelfGameEngine 在 ECS 组件存储中追求极致紧凑（禁用虚函数、手动控制 padding），因为每帧要遍历数百万个组件，缓存命中是生死线。
> UE 则在 `UObject` 体系下接受 vptr 和反射信息的额外开销，因为编辑器工具链需要运行时类型识别和蓝图交互。
> 同一套硬件对齐规则，在两个引擎中导向了不同的设计方向——理解底层，才能判断你的项目该走哪条路。

---

> 相关笔记：
> - [[Notes/C++编程/C++ 虚函数与多态本质|虚函数与多态本质]]
> - [[Notes/C++编程/多重继承的内存布局与 this 指针调整]]
> - [[Notes/C++编程/内存对齐规则与 SIMD 对齐]]
> - [[Notes/C++编程/SoA、AoS 与 AOSOA]]

---
title: 可变字符串与 SSO
date: 2026-06-01
tags:
  - self-game-engine
  - foundation
  - string
  - sso
aliases:
  - Mutable String and SSO
---

> **前置依赖**：[[引擎基础类型与平台抽象]]
> **本模块增量**：深入理解引擎可变字符串的设计空间——从 SSO 原理到三种工业级实现的精确内存布局，从 StringView 的必要性到 COW 的历史教训。
>
> 本笔记是 [[字符串系统]] 话题拆分的子笔记之一，聚焦"可变字符串容器"这个主题。

# 可变字符串与 SSO

## 问题 0：为什么引擎不能直接用 `std::string`？

刚开始写引擎原型时，几乎所有开发者都会下意识选择 `std::string`。它熟悉、安全、功能齐全——直到你开始用 Profiler 看帧时间。

假设你有一个 60 FPS 的游戏，每帧输出 100 条日志，每条日志平均 80 个字符：

```cpp
// 最 naive 的做法：全局使用 std::string
std::string msg = "Player " + name + " pos=(" + std::to_string(x) + ", " + std::to_string(y) + ")";
logger.info(msg);
```

这行代码在运行时发生了什么？

1. `std::to_string(x)` 创建一个临时的 `std::string`，触发堆分配
2. `+` 运算符每次拼接都可能导致重新分配和深拷贝
3. 最终 `msg` 的构造又是一次堆分配
4. 如果 `name` 也是 `std::string`，`"Player " + name` 会先构造一个临时对象

**结果**：单条日志触发了 5~8 次小内存分配。一帧 100 条日志就是 500~800 次分配。每秒 60 帧就是 **3~5 万次堆分配/秒**。

这会带来三个连锁问题：
- **分配器压力**：小对象分配占用了内存分配器的大量时间，且容易产生碎片
- **Cache 不友好**：`std::string` 内部通常是指针+长度+容量的三元组（24 字节），实际字符数据在堆的另一处。遍历字符串时，每次访问都要解引用指针，cache miss 率极高
- **比较成本高**：检查两个资源路径是否相同，需要逐字节比较，O(n) 复杂度。在组件查找、事件订阅键匹配时，这个成本被放大成千上万倍

> **诚实声明**：如果你的引擎目前只是一个窗口加几个立方体，`std::string` 完全够用。本模块解决的是"当字符串操作进入热点路径 Top10 时"的问题。阶段 1 的日志系统用 `std::string` 是正确选择——先让东西跑起来，再优化。
>
> 但需要注意：日志**内容**（长消息）和日志**标签**（级别、分类名）是两类完全不同的数据。前者走[[日志消息与字符串系统的边界|专用流式存储]]，后者才是 SSO/`StringId` 的用武之地。

## 问题 1：如何消除小字符串的堆分配？

观察引擎中的实际字符串使用情况，你会发现一个规律：**80% 的字符串长度小于 16 字节**。组件名（`"Transform"`）、资源短名（`"hero"`）、配置键（`"volume"`）、日志级别（`"DEBUG"`）……这些都不长，但 `std::string` 无一例外地把它们扔到了堆上。

> [!warning] 这个统计不包括格式化后的日志消息
> 日志级别 `"DEBUG"` 很短，但格式化后的日志内容（如 `"Player hero pos=(114.5, 20.3)"`）通常 30~200 字节，远超 SSO 容量。日志消息是**流式临时数据**，不属于 SSO 的优化目标，应采用[[日志消息与字符串系统的边界|环形缓冲区]]等专用结构存储。详见 [[日志消息与字符串系统的边界]]。

### Naive 方案：短字符串走栈，长字符串走堆

```cpp
// 问题：对象大小不固定，无法放入 ECS 组件或数组
struct NaiveString {
    char stack_buf[16];   // 短字符串内联
    char* heap_buf;       // 长字符串堆分配
    size_t len;
    bool is_heap;
};
```

这个方案的问题在于：
- `is_heap` 标志位让对象变成 32 字节（16 + 8 + 8 + 1 对齐到 32），cache line 利用率低
- 条件分支（`if (is_heap)`）在热路径上会降低分支预测命中率
- 放进 ECS 组件时，24 字节的内联空间被浪费了——ECS 组件通常是密集数组

### 改进方案：SSO（Small String Optimization）

SSO 的核心洞察是：**字符串对象本身已经有 24 字节的开销（指针+长度+容量），为什么不把这部分空间用来存字符？**

```cpp
// 教学演示代码——展示核心思想，不是工业实现
class String {
    static constexpr usize SSO_CAPACITY = 15;
    union {
        char  m_inline[SSO_CAPACITY + 1]; // 内联缓冲（16 字节）
        struct {
            char* m_data;     // 8 字节
            usize m_capacity; // 8 字节
        } m_heap;
    };
    usize m_len; // 8 字节

public:
    String(const char* str = "") {
        m_len = std::strlen(str);
        if (m_len <= SSO_CAPACITY) {
            std::memcpy(m_inline, str, m_len + 1);
        } else {
            m_heap.m_capacity = m_len + 1;
            m_heap.m_data = new char[m_heap.m_capacity];
            std::memcpy(m_heap.m_data, str, m_len + 1);
        }
    }
    ~String() { if (!isInline()) delete[] m_heap.m_data; }

    const char* c_str() const { return isInline() ? m_inline : m_heap.m_data; }
    usize length() const { return m_len; }
    bool  isInline() const { return m_len <= SSO_CAPACITY; }
};
```

**这个教学代码的核心思想**：用 union 让同一内存区域在不同模式下存储不同内容——短字符串时存字符数组，长字符串时存指针+容量。

但它有严重缺陷：
- Union 的 strict aliasing 问题：C++ 标准对 union 活跃成员有严格限制
- 缺少 Rule of Five（拷贝/移动构造和赋值）
- 对象大小 32 字节，但 SSO 容量只有 15——空间利用率低
- 没有自定义分配器接口，堆路径直接走 `new/delete`

下面来看三种工业级标准库实现是怎么解决这些问题的。

### 工业实现深度：三种 SSO 方案的精确内存布局

#### libstdc++（GCC）：32 字节，SSO 容量 15

```cpp
// 简化后的 libstdc++ basic_string 布局（x86_64）
template<typename T>
class basic_string {
    struct _Alloc_hider {
        T* _M_p;          // 8 字节：指向数据
    } _M_dataplus;
    
    size_t _M_string_length;  // 8 字节
    
    enum { _S_local_capacity = 15 / sizeof(T) };
    union {
        T _M_local_buf[_S_local_capacity + 1];  // 16 字节
        size_t _M_allocated_capacity;           // 8 字节（覆盖前 8 字节）
    };
};  // 总计 32 字节
```

**关键设计**：
- `_M_dataplus._M_p` 始终指向字符数据。SSO 模式下它指向 `_M_local_buf`
- `_M_string_length` 单独存储，不放在 union 中
- union 中 `_M_allocated_capacity` 只占 8 字节，但 `_M_local_buf` 占 16 字节——堆模式下有 8 字节"死区"
- **空间利用率：15/32 = 47%**

**SSO 模式内存布局**（`"hello"`，长度 5）：

```
+----------+----------+------------------+----------+
| _M_p     | _M_len   | _M_local_buf[16] | (padding)|
| (8B)     | (8B)     | h e l l o \0     | (8B)     |
| 指向本地 | 5        |                  |          |
+----------+----------+------------------+----------+
总计 32 字节
```

#### libc++（Clang）：24 字节，SSO 容量 22

libc++ 的设计更激进——它把整个 24 字节全部用于字符串存储，没有单独的 length/capacity 字段开销。

```cpp
// 简化后的 libc++ basic_string 布局（x86_64）
template<typename T>
class basic_string {
    struct __long {
        size_t __cap_;      // 8 字节
        size_t __size_;     // 8 字节
        T*     __data_;     // 8 字节
    };  // 24 字节

    struct __short {
        union {
            unsigned char __size_;  // 1 字节：存储 SSO 长度
            T __lx;                 // 1 字节：对齐用
        };
        T __data_[23];              // 23 字节
    };  // 24 字节

    union { __long __l; __short __s; } __r_;  // 24 字节
};
```

**关键设计**：
- 整个对象只有一个 24 字节的 union，无额外字段
- **模式判别**：`__long.__cap_` 的最高位（bit 63）置 1。SSO 长度 ≤ 22，所以 `__size_` 的值永远小于 0x80。通过检查第一个字节的最高位即可区分模式

```cpp
bool __is_long() const {
    return __r_.__s.__size_ & 0x80;  // 最高位为 1 → long 模式
}
```

**这个设计的前提假设**：ASCII/UTF-8 字符的最高位都是 0。如果 `__size_` 的最高位是 1，说明是 long 模式的 capacity 字段。

**SSO 模式内存布局**（`"hello"`，长度 5）：

```
+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
|05| h| e| l| l| o| \0|  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 ↑
 __size_ = 5（最高位为 0，SSO 模式）
 剩余 23 字节存储字符（实际可用 22，因为需要末尾 \0）
```

**空间利用率：22/24 = 92%**

#### MSVC STL：32 字节，SSO 容量 15

```cpp
// 简化后的 MSVC basic_string 布局（x86_64）
template<typename T>
class basic_string {
    union _Bxty {
        T _Buf[16];           // 16 字节 SSO 缓冲
        T* _Ptr;              // 8 字节堆指针
        char _Alias[16];      // ABI 兼容保留
    } _Bx;
    
    size_t _Mysize;   // 8 字节
    size_t _Myres;    // 8 字节
};  // 总计 32 字节
```

MSVC 在 2019 版本之前**没有 SSO**。SSO 是后期引入的，这解释了为什么早期 MSVC 的 `std::string` 性能明显差于 GCC/Clang。

### 工业级 SSO 的关键设计决策对比

| 决策点 | libstdc++ | libc++ | MSVC |
|--------|-----------|--------|------|
| 对象大小 | 32 字节 | **24 字节** | 32 字节 |
| SSO 容量（char） | 15 | **22** | 15 |
| 模式判别 | `_M_p` 指向位置 | **首字节最高位** | `_Myres` 值 |
| 空间利用率 | 47% | **92%** | 47% |

**默认推荐**：如果自研引擎追求最小对象体积和最大 SSO 容量，**采信 libc++ 的设计**——24 字节对象 + 首字节模式标记。这也是 folly::fbstring 的设计方向。

### 引擎在可变字符串上的具体方案

| 引擎 | 可变字符串类型 | SSO 策略 | 关键特征 |
|------|--------------|---------|---------|
| **UE** | `FString` | **无内部 union SSO**。用 `TInlineAllocator<4>` 在 `TArray` 层面实现前几个元素的栈存储 | 功能定位接近我们的 `String`，但实现方式不同。`FString` 本质上是 `TArray<TCHAR>`，所有动态增长走 TArray 的分配器参数化路径 |
| **chaos** | 无突出的自定义 String 类型 | 不详 | chaos 更关注格式化系统（`StringFormat`），基础字符串可能直接复用 `std::string` 或平台原语 |
| **Bevy** | 直接用 Rust `String` | Rust `String` 有 SSO 优化，但不是 C++ 意义上的 union SSO，而是小容量时的不同布局策略 | Rust 的 `String` 是指针+长度+容量的三元组，但底层分配器（jemalloc/system allocator）对小分配有优化。Bevy 不在引擎层做字符串存储优化，而是在架构层面减少字符串使用 |

**关键观察**：
- UE 的 `FString` 没有内部 SSO——这是有意为之。UE 选择用分配器参数化来解决问题，让字符串容器本身保持简单，把优化下放到分配器层
- libc++ 的 24 字节 SSO 设计是目前 C++ 标准库中空间效率最高的
- Bevy 的策略是"不在引擎层优化字符串存储，而是在架构层面消灭字符串的热路径使用"

### 自研引擎推荐实现

```cpp
// 推荐：libc++ 风格的 24 字节 SSO + 自定义分配器
class String {
    static constexpr usize SSO_CAP = 22;
    
    union {
        struct { size_t cap; size_t size; char* data; } heap;  // 24 字节
        struct { unsigned char len; char buf[SSO_CAP + 1]; } sso;  // 24 字节
    };
    
    IAllocator* m_allocator;  // 可选：自定义分配器接口

    bool isLong() const { return sso.len & 0x80; }
    
public:
    // 构造/析构/拷贝/移动...（Rule of Five）
    // 堆分配路径：m_allocator->alloc(capacity) 代替 new
};
```

---

## 问题 2：StringView 为什么是必备组件？

主笔记的原始版本中没有提到 `StringView`，这是一个重大遗漏。**工业级字符串系统不可能没有 StringView**。

### 为什么需要 StringView？

想象一个函数接收字符串参数：

```cpp
// 问题：每次调用都触发 String 的构造（甚至堆分配）
void processName(const String& name);

// 调用方
processName("Player_1");           // 隐式构造 String
processName(otherString);          // 引用绑定，无拷贝
processName(stringId.resolve(id)); // resolve 返回 const char*，又触发 String 构造
```

即使 SSO 避免了堆分配，每次隐式构造仍然有 `strlen` + `memcpy` 的开销。更严重的是子串操作：

```cpp
// 子串操作：如果返回 String，必须拷贝
String extension = path.substr(path.find_last_of('.')); // 拷贝 N 字节
```

`StringView` 解决的核心问题是：**在不拥有字符数据的前提下，安全地引用一段连续的字符序列**。

### StringView 的工业级设计

```cpp
class StringView {
    const char* m_data;
    usize m_len;

public:
    constexpr StringView(const char* str = "")
        : m_data(str), m_len(__constexpr_strlen(str)) {}
    constexpr StringView(const char* str, usize len)
        : m_data(str), m_len(len) {}
    StringView(const String& str)
        : m_data(str.c_str()), m_len(str.length()) {}

    constexpr const char* data() const { return m_data; }
    constexpr usize length() const { return m_len; }
    constexpr bool empty() const { return m_len == 0; }
    
    constexpr char operator[](usize i) const { return m_data[i]; }
    
    constexpr StringView substr(usize pos, usize n = npos) const {
        return StringView(m_data + pos, min(n, m_len - pos));
    }
    
    constexpr bool operator==(StringView o) const {
        return m_len == o.m_len && __memcmp(m_data, o.m_data, m_len) == 0;
    }

    static constexpr usize npos = ~usize(0);
};
```

**关键设计决策**：

1. **不保证 null-terminated**：`StringView` 的数据不一定以 `\0` 结尾。不能直接把 `m_data` 传给 `strlen` 或 C API。

2. **生命周期管理**：`StringView` 不拥有数据，也不延长被引用字符串的生命周期。这是使用者必须遵守的契约。

3. **从临时对象构造的风险**：
   ```cpp
   StringView bad() {
       String s = "temporary";
       return StringView(s);  // 悬垂引用！
   }
   ```

4. **constexpr 支持**：`StringView` 的全部操作都应该是 `constexpr`，使其能在编译期使用。

### StringView 在引擎中的使用边界

| 场景 | 参数类型 | 理由 |
|------|---------|------|
| 只读访问字符串内容 | `StringView` | 零拷贝、无分配、可接受字面量/String/char* |
| 需要存储字符串副本 | `String` | StringView 不拥有数据 |
| 需要修改字符串 | `String&` | StringView 是只读的 |
| 资源路径/组件名标识 | `StringId` | O(1) 比较，比 StringView 的 O(n) 更快 |
| 日志格式化参数 | `StringView` | 避免构造临时 String |

> **核心原则**：引擎中所有只读字符串参数默认用 `StringView`，只有在需要存储或修改时才用 `String`。

---

## 问题 3：COW（Copy-On-Write）的历史与废弃

理解 COW 的历史对设计 String 至关重要。GCC 的 libstdc++ 在 C++11 之前使用 COW，C++11 标准明确禁止了 `std::string` 的 COW 实现。

### 什么是 COW？

```cpp
// COW 字符串的核心思想：拷贝时共享数据，写入时复制
String a = "hello";
String b = a;        // 不拷贝，b 和 a 共享同一块堆内存
b[0] = 'H';          // 写入触发复制：b 现在有自己的独立拷贝
```

COW 通过引用计数实现：

```cpp
struct StringData {
    uint32_t refCount;
    uint32_t capacity;
    uint32_t length;
    char data[];  // 柔性数组成员
};
```

### 为什么 C++11 禁止了 std::string 的 COW？

**多线程下的引用计数原子操作成本超过了 COW 的收益**。

```cpp
// COW 的致命问题：每次拷贝都要原子操作引用计数
String b = a;  // atomic increment(refCount) — 内存屏障，缓存同步

// 在现代 CPU 上，原子操作的成本大约是 20~40 个时钟周期
// 而 memcpy 22 字节的 SSO 字符串只需要 2~4 个时钟周期
```

场景分析：

```
场景 1：单线程，大量拷贝
  COW:  拷贝 = atomic_inc (30 cycles)
  SSO:  拷贝 = memcpy 24 bytes (4 cycles)
  → COW 慢 7 倍！

场景 2：多线程，少量拷贝
  COW:  拷贝 = atomic_inc (30 cycles，缓存行在 CPU 间弹跳)
  SSO:  拷贝 = memcpy 24 bytes (4 cycles，完全无共享)
  → COW 更慢，且引入缓存行竞争

场景 3：超大字符串（1MB），频繁拷贝
  COW:  拷贝 = atomic_inc (30 cycles)，共享 1MB 数据
  SSO:  无法内联 1MB，必须堆分配 + 深拷贝 (memcpy 1MB ≈ 100,000 cycles)
  → COW 在这个极端场景下才有优势
```

**结论**：对引擎中的绝大多数字符串（短字符串、频繁拷贝），COW 比 SSO 慢。现代 C++ 字符串设计的共识是 **SSO 取代 COW**。

### 引擎中 COW 还有用武之地吗？

**非常有限**：

1. **超大文本块**：如 JSON 配置文件全文。如果确定只读共享，可用 `std::shared_ptr<std::string>`
2. **不可变字符串池**：如 InternPool 中的字符串——但这不是 COW，是"只创建一次，永不修改"

**自研引擎的决策**：
- `String`（SSO）：可变字符串，拷贝即深拷贝（SSO 模式下是 cheap copy），无引用计数
- `StringView`：只读引用，零拷贝，无所有权
- InternPool 中的字符串：不可变，多线程安全读取

---

## ECS 映射：可变字符串在 ECS 中该怎么用？

```cpp
// ❌ 反模式：组件存储 String
struct NameComponent {
    String name; // 24-32 字节，可能含堆指针
};
```

如果 `NameComponent` 包含 `String`，ECS 的密集数组存储会被破坏：
- 组件数组不再是一个连续的 `NameComponent[N]`，因为某些元素可能指向堆
- 序列化时需要特殊处理指针字段
- AI 观察时无法简单地"把组件数据平铺成 JSON"

```cpp
// ✅ 正确：组件存储 StringId，System 参数用 StringView
struct NameComponent {
    StringId nameId; // u64，纯值，无指针
};

void spawnEntity(SystemContext ctx, StringView nameTemplate) {
    // nameTemplate 可以是字面量、String、或其他 StringView
    StringId id = ctx.internPool.intern(nameTemplate);
    ctx.world.add<NameComponent>(entity, {id});
}
```

---

> **下一步**：[[字符串标识与 InternPool]] — 字符串消除了堆分配后，下一个问题是：如何让字符串比较从 O(n) 降到 O(1)？

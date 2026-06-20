---
order: 11
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

> **前置依赖**：[[引擎基础类型与平台抽象]]、[[字符串系统]]
> **本模块增量**：深入理解可变字符串容器的三种工业级路径——union SSO、分配器参数化、薄封装 std::string（chaos 路线）的本质差异、适用场景与在 ECS 架构下的可行性。
>
> 本笔记是 [[字符串系统]] 话题拆分的子笔记之一，聚焦"可变字符串容器"这个主题。关于字符串在 ECS 中的角色分工，详见 [[字符串系统#三种字符串角色的精确定义]]。
>
> **实现状态**（2026-06-02）：Entelechy 已从自研 union-SSO 切换为 **chaos 薄封装路线**（`std::basic_string` + `StdAllocatorAdapter`）。详见文末「实现细节」。

# 可变字符串与 SSO

## 问题 0：为什么引擎不能直接用 `std::string`？

`std::string` 对引擎而言有两个核心缺陷：

1. **分配器不可控**：`std::string` 的堆分配走全局 `new/delete`，无法接入引擎的内存池、线程局部分配器或追踪系统。一帧 100 条日志、每条触发 5~8 次小分配，每秒就是 **3~5 万次堆分配/秒**
2. **不应放入 ECS 组件**：即使 SSO 让短字符串零堆分配，`std::string` 仍不是纯值类型——长字符串会触发堆分配，SSO 模式分支在热路径上成为不可预测的分支。ECS 组件中的字符串应该是 `StringId`，不是 `String`

> **诚实声明**：如果你的引擎目前只是一个窗口加几个立方体，`std::string` 完全够用。本模块解决的是"当字符串操作进入热点路径 Top10 时"的问题。阶段 1 的日志系统用 `std::string` 是正确选择——先让东西跑起来，再优化。

---

## 问题 1：可变字符串的两种工业级优化路径

消除小字符串堆分配的两种路线，本质上是"优化放在容器内部还是容器外部"的分歧。

### 路径 A：union SSO（标准库路线）

**核心洞察**：字符串对象本身已经有 24 字节的开销（指针+长度+容量），为什么不把这部分空间用来存字符？

三种标准库实现的差异：

| 实现 | 对象大小 | SSO 容量 | 模式判别 | 空间利用率 |
|------|---------|---------|---------|-----------|
| libstdc++（GCC） | 32 字节 | 15 | `_M_p` 指向位置 | 47% |
| libc++（Clang） | **24 字节** | **22** | **首字节最高位** | **92%** |
| MSVC STL | 32 字节 | 15 | `_Myres` 值 | 47% |

libc++ 的设计最激进：整个 24 字节只有一个 union，没有单独的 length/capacity 字段。SSO 模式下首字节存长度（值 ≤ 22，最高位为 0）；long 模式下首字节被 capacity 覆盖（最高位为 1）。一个 bit 区分两种模式。

```cpp
// libc++ 风格的核心思想（教学代码）
class String {
    union {
        struct { size_t cap; size_t size; char* data; } heap;  // 24B
        struct { unsigned char len; char buf[23]; } sso;       // 24B
    };
    bool isLong() const { return sso.len & 0x80; }
};
```

**union SSO 的适用边界**：
- ✅ 通用容器库、跨项目复用（folly::fbstring 走的就是这条路）
- ✅ 对象大小必须固定（如放在 `std::vector<String>` 中）
- ❌ 需要自定义分配器时，union 的布局和分配器接口耦合较深
- ❌ UTF-8 多字节字符会快速耗尽 SSO 容量（一个汉字 3 字节，22 字节缓冲只能存 7 个汉字）

### 路径 B：分配器参数化（UE 路线）

UE 的 `FString` 没有内部 union SSO。它的实现是：

```cpp
// FString 的实际定义（UnrealString.h.inl，第 54-69 行）
class FString {
    using AllocatorType = TSizedDefaultAllocator<32>;  // 纯堆分配器
    using ElementType   = TCHAR;
    typedef TArray<ElementType, AllocatorType> DataType;
    DataType Data;  // FString 内部就是一个 TArray<TCHAR>
};
```

`TSizedDefaultAllocator<32>` 中的 `<32>` 是**对齐字节数（alignment）**，不是内联存储大小。它本质是 `TSizedHeapAllocator<32>`——**纯堆分配，没有内联存储**。一个空的 `FString` 内部是 `ArrayNum=0, ArrayMax=0, ArrayData=nullptr`。也就是说，`FString("hi")` 这个 3 字符的字符串，照样会触发一次堆分配来存储 `"hi\0"`。

但 UE 的 `TArray` 支持**分配器模板参数化**：

```cpp
// 可选：用 TInlineAllocator 把前 4 个元素放在对象内部
TArray<int32, TInlineAllocator<4>> SmallArray;
```

`TInlineAllocator<N>` 的内存布局比 union SSO 更"笨重"——它在对象内部同时保留了**两套存储**：

```cpp
// TInlineAllocator<4, int32> 的简化示意（x64）
class TInlineAllocator_4_int32 {
    union {
        int32 InlineData[4];   // 内联数组：16 字节
        int32*  HeapData;      // 堆指针：8 字节
    };
    int32 NumInline;             // 当前用了几个内联槽位：4 字节
    int32 NumAllocated;          // 总分配数（内联或堆）：4 字节
    // 对象总大小 ≈ 16 + 8 + 4 + 4 = 32 字节（含 padding）
};
```

关键点：**union 里同时存在 `InlineData[4]` 和 `HeapData` 指针，但同一时间只用其中一个**。当 `NumInline > 0` 时，数据在内联数组；当 `NumInline == 0` 且 `HeapData != nullptr` 时，数据在堆上。

这意味着 mode switch（从内联切到堆）时，必须执行一次 `memcpy` 把内联数组的内容搬到堆上新分配的内存。反过来（堆缩回内联）也要 `memcpy`。这个搬迁成本在短字符串高频构造/析构场景下不可忽视——而 union SSO 的切换只是同一个 24 字节 union 的 reinterpret，零拷贝。

**这和 union SSO 的本质区别**：

| 维度     | union SSO             | TInlineAllocator             |
| ------ | --------------------- | ---------------------------- |
| 内联空间来源 | union 中固定大小的字符缓冲      | 对象内部独立数组 + 指针                |
| 模式切换代价 | 无（同一内存区域 reinterpret） | 需要 `memcpy` 搬迁数据             |
| 对象大小   | 固定（24/32 字节）          | 固定（内联数组 + 指针 + 元数据）          |
| 分配器可控性 | 低（union 布局与分配器耦合）     | **高**（堆路径走自定义分配器）            |
| 容器复用性  | 仅字符串专用                | **通用**（所有 TArray 共享同一套分配器体系） |

UE 选择分配器参数化的原因是：**它把"要不要内联存储"的决策权交给调用方，而不是硬编码在字符串类型里**。对需要频繁拼接的局部字符串，用默认堆分配器；对确定很小的数组，显式用 `TInlineAllocator<N>`。

### 路径 C：薄封装 + 自定义分配器（chaos 路线）

chaos 没有自研 union SSO，而是直接封装 `std::basic_string`：

```cpp
// chaos/core/std/string.h，第 59-68 行
class String {
#if CHAOS_USE_CMP
    typedef std::basic_string<char, std::char_traits<char>, 
                              AllocatorWithFixedPool<char, StringPool>> STDString;
#else
    typedef std::basic_string<char> STDString;
#endif
    STDString m_data;
};
```

`AllocatorWithFixedPool` 把 `std::basic_string` 的字符数组分配导向 chaos 自己的内存池。它的工作方式类似一个**线程局部的 bump allocator**：

```cpp
// 极度简化的原理示意
template<typename T, typename Pool>
class AllocatorWithFixedPool {
    Pool& pool;  // 引用一个 64KB 的固定内存池
public:
    T* allocate(size_t n) {
        size_t bytes = n * sizeof(T);
        if (pool.has_space(bytes)) {
            return pool.bump_alloc(bytes);  // 从池顶切一块，无锁、无系统调用
        }
        return ::operator new(bytes);       // 池满了，fallback 到全局 new
    }
    void deallocate(T* p, size_t) {
        if (pool.contains(p)) return;        // 池内内存不单独回收（池整体重置时一并释放）
        ::operator delete(p);                // 全局 new 的内存，走全局 delete
    }
};
```

注意：**`StringPool` 是内存分配池，不是字符串内容去重池**——它只负责"从哪块内存里切出字符数组"，不负责"这个字符串内容是否已经存在"。两个内容完全相同的 `"Player_1"` 字符串，各自独立存储，不会合并。

chaos 的策略本质是：**接受 std::string 的全部语义（包括其可能有的平台级 SSO），只在分配器层做优化**。这是一条"低成本、低风险"的路线，但意味着字符串对象的行为完全依赖底层 STL 实现。

### 三引擎对照总结

| 引擎 | 路线 | 可变字符串类型 | 核心特征 | 组件中存什么 |
|------|------|-------------|---------|-----------|
| **UE** | 分配器参数化 | `FString` = `TArray<TCHAR, 堆分配器>` | 无内部 SSO，通过 `TInlineAllocator` 让调用方按需内联 | `FName`（全局字符串池句柄）|
| **chaos** | 薄封装 + 自定义分配器 | `String` = `std::basic_string` 封装 | 可挂 `AllocatorWithFixedPool` 内存池 | `StringID`（编译期 FNV-1a 哈希）|
| **Bevy** | 架构规避 | Rust `String`（无 SSO）| 接受 `String` 无 SSO 的现实，用 `Cow`/`Hashed`/`CowArc`/`Interned` 减少热路径使用 | `Name` = `Hashed<Cow<'static, str>>` |

**关键洞察**：三家引擎在"可变字符串容器是否要有 SSO"这个问题上，答案是——**不重要**。真正重要的是：标识性字符串用 `StringId`/`FName`，只读引用用 `StringView`，局部可变文本用 `String`/`FString`，但不让它进入 ECS 组件。

---

## 问题 2：StringView 为什么是必备组件？

`StringView` 解决的核心问题是：**在不拥有字符数据的前提下，安全地引用一段连续的字符序列**。

```cpp
// 问题：每次调用都触发 String 的构造（哪怕 SSO 也要 strlen + memcpy）
void processName(const String& name);
processName("Player_1");  // 隐式构造 String

// 解决：用 StringView 接收任意字符串来源
void processName(StringView name);  // 零拷贝
processName("Player_1");            // 字面量直接引用
processName(otherString);           // 绑定已有 String
processName(path.substr(0, 4));     // 子串零拷贝
```

### 工业级设计要点

```cpp
class StringView {
    const char* m_data;
    usize m_len;

public:
    constexpr StringView(const char* str = "")
        : m_data(str), m_len(__constexpr_strlen(str)) {}
    constexpr StringView(const char* str, usize len)
        : m_data(str), m_len(len) {}

    constexpr const char* data() const { return m_data; }
    constexpr usize length() const { return m_len; }
    constexpr StringView substr(usize pos, usize n = npos) const {
        return StringView(m_data + pos, min(n, m_len - pos));
    }

    static constexpr usize npos = ~usize(0);
};
```

**关键约束**：
1. **不保证 null-terminated**：`StringView` 的数据不一定以 `\0` 结尾，不能传给 `strlen` 或 C API
2. **生命周期契约**：`StringView` 不延长被引用字符串的生命周期，从临时对象构造会导致悬垂引用
3. **constexpr 全支持**：所有操作应在编译期可用

关于 `StringView` 在引擎中的完整使用边界，详见 [[字符串系统#默认推荐路径]]。

---

## 问题 3：COW 已死，无需留恋

C++11 之前，GCC 的 libstdc++ 使用 Copy-On-Write（引用计数共享数据，写入时复制）。C++11 标准明确禁止了 `std::string` 的 COW 实现。

**原因一句话**：多线程下每次拷贝都要原子操作引用计数（20~40 周期），而 memcpy 24 字节的 SSO 字符串只需 2~4 周期。在引擎的短字符串高频拷贝场景中，COW 比 SSO 慢 7 倍以上，且引入缓存行竞争。

**现代共识**：SSO 取代 COW。引擎中唯一需要共享大文本块的场景，用 `std::shared_ptr<std::string>` 或 InternPool 中的不可变字符串即可。

---

## 自研引擎的推荐方案

| 条件 | 推荐方案 |
|------|---------|
| 追求最小实现成本 | **薄封装 `std::string` + 自定义分配器**（chaos 路线）。接受平台 std::string 的 SSO（如有），只在分配器层接入引擎内存池 |
| 追求极致性能且团队有能力维护 | **自研 union SSO**（libc++ 风格 24 字节 + 首字节模式标记），或接入 folly::fbstring |
| 需要同时支持多种分配策略 | **分配器参数化**（UE 路线），容器本身简单，优化下放到分配器层 |

> [!tip] 推荐路线：chaos 薄封装
> 阶段 1~2 **明确采用 chaos 路线**——薄封装 `std::string` + 自定义分配器。代码已于 2026-06-02 完成迁移。

原因：
- **实现成本最低**：不重复造轮子，接口直接复用 `std::string` 的全部语义（`substr`、`find`、`append` 等），团队零学习成本
- **短字符串性能不降级**：现代标准库（GCC/Clang/MSVC2019+）的 `std::string` 已有成熟的 union SSO，22 字符以内零堆分配。UE 的默认 `FString` 反而在这里会触发堆分配
- **长字符串分配可控**：通过 `StdAllocatorAdapter` 把 `std::basic_string` 的堆分配导向引擎的 `DefaultAllocator`（Mimalloc 或平台对齐分配），获得追踪、统计的全部优势
- **消除 UB**：自研 union SSO 存在 strict aliasing 和 active-member-switch 的未定义行为风险，`std::string` 的实现经过充分验证
- **迁移无感知**：薄封装层的接口稳定，底层始终通过 `std::basic_string` 公共接口操作，调用方代码无需关心平台差异

 UE 的分配器参数化路线更适合**已经有一个成熟分配器体系、且团队愿意在热路径上显式选择 `TInlineAllocator<N>`** 的项目。对于从零开始的自研引擎，chaos 路线的 ROI 明显更高。

关于 `String` 在 ECS 中的使用边界（为什么组件中不能存 `String`），详见 [[字符串系统#ECS 映射]]。

### 实现细节（Entelechy 当前代码）

```cpp
// _engine/source/core/public/allocator/std_allocator_adapter.h
template<typename T, typename EngineAllocatorT>
class StdAllocatorAdapter { /* 标准 allocator 接口适配器 */ };

// _engine/source/core/public/string/string.h
template<typename AllocatorT = DefaultAllocator>
class BasicString {
    using AllocType = StdAllocatorAdapter<char, AllocatorT>;
    std::basic_string<char, std::char_traits<char>, AllocType> m_str;
    // ... 全部 API 转发给 m_str ...
};

using String = BasicString<DefaultAllocator>;
```

**`isInline()` 语义变更**：因 `std::string` 不暴露实际存储位置，且不同 STL 的 SSO 阈值不同，`isInline()` 从"是否实际 inline 存储"改为"长度是否 ≤ SSO_CAPACITY（15）"。全局搜索确认生产代码无 `isInline()` 调用，仅测试使用。

---

> **下一步**：[[字符串标识与 InternPool]] — 可变字符串的角色被限死后，如何让标识性字符串的比较从 O(n) 降到 O(1)？

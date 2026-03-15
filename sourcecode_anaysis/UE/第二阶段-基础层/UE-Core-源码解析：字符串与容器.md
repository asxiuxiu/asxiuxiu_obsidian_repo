---
title: UE-Core-源码解析：字符串与容器
date: 2026-04-17
tags:
  - ue-source
  - engine-architecture
aliases:
  - UE-Core 字符串与容器
---

> [[00-UE全解析主索引|← 返回 UE全解析主索引]]

# UE-Core-源码解析：字符串与容器

## 模块定位

- **UE 模块路径**：`Engine/Source/Runtime/Core/`
- **核心目录**：`Public/String/`、`Public/Containers/`
- **核心头文件**：`Containers/Array.h`、`Containers/UnrealString.h`、`Containers/Map.h`、`Containers/Set.h`
- **模块角色**：提供 UE 替代 STL 的字符串与容器体系，强调内存可控、序列化友好、平台一致。

---

## 接口梳理（第 1 层）

### 核心容器概览

| 头文件 | 模板类 | 职责 | 对应 STL |
|--------|--------|------|---------|
| `Array.h` | `TArray<T, Allocator>` | 动态数组 | `std::vector` |
| `UnrealString.h` | `FString` | 动态可变字符串 | `std::wstring` / `std::string` |
| `Map.h` | `TMap<Key, Value>` | 哈希映射表 | `std::unordered_map` |
| `Set.h` | `TSet<T>` | 哈希集合 | `std::unordered_set` |
| `BitArray.h` | `TBitArray<>` | 位压缩数组 | 无直接对应 |
| `SparseArray.h` | `TSparseArray<T>` | 稀疏索引数组（支持稳定句柄） | 无直接对应 |

### 核心字符串类

| 类名 | 特点 | 使用场景 |
|------|------|---------|
| `FString` | `TArray<TCHAR>` 封装，可变、可格式化 | 运行时动态字符串 |
| `FText` | 支持本地化与文本收集 | UI 显示文本 |
| `FName` | 全局字符串池，大小写不敏感，比较 O(1) | 标识符、资源路径、反射名称 |

### FString 的接口边界

> 文件：`Engine/Source/Runtime/Core/Public/Containers/UnrealString.h`，第 1~22 行

```cpp
#define UE_STRING_CLASS                        FString
#define UE_STRING_CHARTYPE                     TCHAR
#define UE_STRING_CHARTYPE_IS_TCHAR            1
#define UE_STRING_PRINTF_FMT_CHARTYPE          TCHAR
    #include "Containers/UnrealString.h.inl"
```

`FString` 的实际实现放在 `.inl` 中，通过宏参数化，允许生成 `FString`（`TCHAR` 版本）或其他字符类型的字符串类。

---

## 数据结构（第 2 层）

### TArray 的内存布局

`TArray` 采用与 `std::vector` 类似的**三指针/双整数**布局（取决于分配器）：

```cpp
// 概念性布局（以 FHeapAllocator 为例）
template<typename T>
class TArray
{
    T*   ArrayData;    // 指向堆分配内存
    int32 ArrayNum;    // 当前元素数
    int32 ArrayMax;    // 分配容量
};
```

> 文件：`Engine/Source/Runtime/Core/Public/Containers/ContainerAllocationPolicies.h`，第 138~220 行

```cpp
template <typename SizeType>
UE_FORCEINLINE_HINT SizeType DefaultCalculateSlackGrow(
    SizeType NewMax, SizeType CurrentMax, SIZE_T BytesPerElement, bool bAllowQuantize, uint32 Alignment = DEFAULT_ALIGNMENT)
{
    const SIZE_T FirstGrow = 4;
    const SIZE_T ConstantGrow = 16;
    
    SIZE_T Grow = FirstGrow;
#if CONTAINER_INITIAL_ALLOC_ZERO_SLACK
    if (CurrentMax)
    {
        Grow = SIZE_T(NewMax) + 
               UE_CONTAINER_SLACK_GROWTH_FACTOR_NUMERATOR * SIZE_T(NewMax) / UE_CONTAINER_SLACK_GROWTH_FACTOR_DENOMINATOR 
               + ConstantGrow;
    }
    else if (SIZE_T(NewMax) > Grow)
    {
        Grow = SIZE_T(NewMax);
    }
#endif
    // ... QuantizeSize
}
```

**增长策略**：
- 首次分配 4 个元素（或按需 0 slack）。
- 后续按 `NewMax + (3/8)*NewMax + 16` 增长（默认配置）。
- 通过 `FMemory::QuantizeSize` 对齐到分配器粒度，减少内部碎片。

### TSet / TMap 的底层结构

`TSet` 基于**开放寻址法**的哈希表实现：
- `Hash` 数组存储哈希值（用于快速拒绝）。
- `Elements` 为 `TSparseArray`（稀疏数组），存储实际元素。
- 支持稳定句柄（`FSetElementId`），删除元素时不会使其他句柄失效。

`TMap` 内部就是 `TSet<TPair<Key, Value>>` 的薄封装。

### FString 的数据层

`FString` 继承自 `TArray<TCHAR>`，因此内存布局与 `TArray` 完全一致：

```cpp
class FString : private TArray<TCHAR>
{
public:
    // 提供字符串特有的操作：Printf、Split、Trim、Replace、Path 操作等
};
```

### FName 的字符串池

`FName` 内部仅存储一个 `FNameEntryId`（整数索引）和一个 `Number`（用于处理后缀如 `_1`、`_2`）。

所有字符串实际存储在全局的 `FNamePool` 中：
- **分块分配**：按字符串长度分块（`FNES_Short` / `FNES_Medium` / `FNES_Large`）。
- **无锁读取**：`FName` 比较只需比较两个整数。
- **线程安全写入**：通过原子操作和锁保护插入新字符串。

---

## 行为分析（第 3 层）

### TArray 的扩容调用链

1. `TArray::Add` / `Emplace` → 检查 `ArrayNum < ArrayMax`
2. 若不足，调用分配器的 `ResizeAllocation`
3. `ResizeAllocation` 调用 `FMemory::Realloc`
4. `FMemory::Realloc` 最终落入平台特定的 `FMalloc`（如 `FMallocBinned3`）

> 文件：`Engine/Source/Runtime/Core/Public/Containers/ContainerAllocationPolicies.h`，第 138~166 行

```cpp
template <typename SizeType>
UE_FORCEINLINE_HINT SizeType DefaultCalculateSlackShrink(...)
{
    const bool bTooManySlackBytes = CurrentSlackBytes >= 16384;
    const bool bTooManySlackElements = 3 * NewMax < 2 * CurrentMax;
    if ((bTooManySlackBytes || bTooManySlackElements) && (CurrentSlackElements > 64 || !NewMax))
    {
        Retval = NewMax; // 缩容到精确大小
    }
    else
    {
        Retval = CurrentMax; // 保持容量
    }
}
```

**缩容策略**：当空闲字节 >= 16KB 或空闲比例超过 33% 时，才触发缩容。

### FString 的格式化行为

`FString::Printf` 内部调用 `FCString::Sprintf` 或 `TSprintf`（安全格式化），支持 `TCHAR` 字符集：

```cpp
static FString Printf(const TCHAR* Fmt, ...)
{
    FString Result;
    GET_VARARGS_RESULT(Result, Fmt, Fmt);
    return Result;
}
```

### TSet 的 Rehash 流程

1. 计算新 bucket 数量（通常为素数表中的下一个值）。
2. 分配新的 `Hash` 数组并标记为 `Empty`。
3. 遍历 `TSparseArray` 中的每个存活元素：
   - 重新计算 hash → 线性探测找到空槽 → 写入 `HashIndex`。
4. 更新 `HashSize` 与 `Hash` 指针。

---

## 与上下层的关系

### 上层调用者

- **反射系统**：`FName` 是 `UClass`、`UProperty`、`UFunction` 命名的基石。
- **序列化系统**：`TArray`、`TMap`、`FString` 都实现了 `Serialize(Ar)` 接口。
- **Slate / UMG**：大量使用 `FText` 做本地化显示。

### 下层依赖

- **内存分配器**：`TArray` 最终依赖 `FMemory` / `FMalloc`。
- **平台字符串 API**：`FString` 的转换操作依赖 `GenericPlatformString`。

---

## 设计亮点与可迁移经验

1. **分配器参数化**：`TArray<T, Allocator>` 的第二个模板参数允许自定义分配策略（如 `TInlineAllocator<4>` 将前 4 个元素放在栈上），这是比 STL 更灵活的设计。
2. **增长因子的工程折中**：`3/8`（37.5%）的增长比例在内存与拷贝开销之间取得平衡，且通过 `QuantizeSize` 进一步对齐到分配器桶大小。
3. **FName 字符串池**：将字符串比较从 O(n) 降为 O(1)，对大规模资源路径比较（如 AssetRegistry）至关重要。
4. **SparseArray 提供稳定 ID**：`TSparseArray` 结合 `TSet` 实现了"删除不使句柄失效"的哈希表，这对需要持久引用元素的游戏对象集合非常有价值。

---

## 关键源码片段

> 文件：`Engine/Source/Runtime/Core/Public/Containers/ContainerAllocationPolicies.h`，第 168~220 行

```cpp
template <typename SizeType>
UE_FORCEINLINE_HINT SizeType DefaultCalculateSlackGrow(
    SizeType NewMax, SizeType CurrentMax, SIZE_T BytesPerElement, bool bAllowQuantize, uint32 Alignment = DEFAULT_ALIGNMENT)
{
    const SIZE_T FirstGrow = 4;
    const SIZE_T ConstantGrow = 16;
    SIZE_T Grow = FirstGrow;
    
    if (CurrentMax)
    {
        Grow = SIZE_T(NewMax) + UE_CONTAINER_SLACK_GROWTH_FACTOR_NUMERATOR 
               * SIZE_T(NewMax) / UE_CONTAINER_SLACK_GROWTH_FACTOR_DENOMINATOR + ConstantGrow;
    }
    else if (SIZE_T(NewMax) > Grow)
    {
        Grow = SIZE_T(NewMax);
    }

    if (bAllowQuantize)
    {
        Retval = (SizeType)(FMemory::QuantizeSize(Grow * BytesPerElement, Alignment) / BytesPerElement);
    }
    // ...
}
```

---

## 关联阅读

- [[UE-Core-源码解析：基础类型与宏体系]]
- [[UE-Core-源码解析：内存分配器家族]]
- [[UE-Core-源码解析：配置与命令行]]

---

## 索引状态

- **所属阶段**：第二阶段 — 基础层源码解析 / 2.1 Core 基础类型与工具
- **对应笔记**：UE-Core-源码解析：字符串与容器
- **本轮完成度**：✅ 第三轮（接口层 + 数据层 + 逻辑层）
- **更新日期**：2026-04-17

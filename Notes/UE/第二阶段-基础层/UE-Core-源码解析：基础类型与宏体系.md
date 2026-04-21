---
title: UE-Core-源码解析：基础类型与宏体系
date: 2026-04-17
tags:
  - ue-source
  - engine-architecture
aliases:
  - UE-Core 基础类型与宏体系
---

> [[00-UE全解析主索引|← 返回 UE全解析主索引]]

# UE-Core-源码解析：基础类型与宏体系

## 模块定位

- **UE 模块路径**：`Engine/Source/Runtime/Core/`
- **Build.cs 文件**：`Core.Build.cs`
- **核心头文件**：`Public/CoreTypes.h`、`Public/CoreMinimal.h`、`Public/Misc/CoreMiscDefines.h`
- **模块角色**：UE 的"标准库"层，所有上层模块（Engine、Editor、RHI）的编译基石。

Core 模块没有外部 Public 依赖（仅暴露给引擎各模块），其底层又通过 `HAL/` 目录对接操作系统与硬件。

---

## 接口梳理（第 1 层）

### 公共头文件与职责

| 头文件 | 核心内容 | 职责 |
|--------|---------|------|
| `CoreTypes.h` | `int32`、`uint32`、`TCHAR`、`ANSICHAR`、平台宏 | 定义 UE 跨平台基础类型别名 |
| `CoreMinimal.h` | 聚合包含 Array、String、Math、Delegate、SharedPtr 等 | 提供 90% 以上日常开发所需的最小头文件集合 |
| `CoreMiscDefines.h` | `UE_DEPRECATED`、`FORCEINLINE`、`PURE_VIRTUAL`、`INDEX_NONE` | 编译期控制宏与工具宏 |
| `HAL/Platform.h` | `PLATFORM_WINDOWS`、`PLATFORM_CPU_X86_FAMILY`、编译器检测 | 平台抽象与 SIMD 能力探测 |

### 关键宏体系

> 文件：`Engine/Source/Runtime/Core/Public/Misc/CoreMiscDefines.h`，第 150~165 行

```cpp
enum {INDEX_NONE  = -1          };
enum {UNICODE_BOM = 0xfeff      };

enum EForceInit { ForceInit, ForceInitToZero };
enum ENoInit    { NoInit };
enum EConstEval { ConstEval };
enum EInPlace   { InPlace };
enum EPerElement{ PerElement };
```

这些枚举被广泛用于容器初始化语义控制（如 `TArray(ForceInit)`、矩阵构造）。

> 文件：`Engine/Source/Runtime/Core/Public/Misc/CoreMiscDefines.h`，第 200~240 行

```cpp
#define UE_DEPRECATED(Version, Message) [[deprecated(Message)]]
```

`UE_DEPRECATED` 是 UE 统一的弃用标记宏，支持函数、类型、变量、命名空间级别。

---

## 数据结构（第 2 层）

### 平台类型映射

`CoreTypes.h` 本身并不直接定义 `int32` 等，而是将底层 `HAL/Platform.h` 中的平台相关类型重新导出：

```cpp
// CoreTypes.h 第 8~14 行
#include "HAL/Platform.h"
#include "ProfilingDebugging/UMemoryDefines.h"
#include "Misc/CoreMiscDefines.h"
#include "Misc/CoreDefines.h"
```

平台头文件（如 `Windows/WindowsPlatform.h`）会定义：
- `int8` / `int16` / `int32` / `int64`
- `uint8` / `uint16` / `uint32` / `uint64`
- `SIZE_T`
- `TCHAR`（Windows 下为 `WCHAR`，Unix 下为 `char16_t` 或 `wchar_t`）

### `CoreMinimal.h` 的包含拓扑

`CoreMinimal.h` 是 UE 的头文件"心脏"，包含约 100+ 个头文件，按功能分层：
1. **类型 traits**：`IsPointer.h`、`UnrealTypeTraits.h`、`EnableIf.h`
2. **内存与平台**：`PlatformMemory.h`、`PlatformAtomics.h`、`UnrealMemory.h`
3. **字符串**：`UnrealString.h`、`CString.h`、`StringConv.h`
4. **容器**：`Array.h`、`Map.h`、`Set.h`、`BitArray.h`、`SparseArray.h`
5. **数学**：`Vector.h`、`Vector4.h`、`Rotator.h`、`Quat.h`、`Transform.h`
6. **委托与指针**：`Delegate.h`、`SharedPointer.h`、`WeakObjectPtrTemplates.h`
7. **国际化**：`Text.h`、`Internationalization.h`

### 内存布局特点

- `CoreMinimal.h` 不引入任何 `UObject` 反射头文件（无 `UCLASS` 宏），因此可用于非 UObject 的纯 C++ 模块。
- 通过 `IWYU`（Include What You Use）注释控制导出范围，减少隐式包含污染。

---

## 行为分析（第 3 层）

### 编译期平台能力探测流程

> 文件：`Engine/Source/Runtime/Core/Public/HAL/Platform.h`，第 70~130 行

```cpp
// 1. 首先包含 PreprocessorHelpers.h 与平台特定的 PlatformCompilerPreSetup.h
#include COMPILED_PLATFORM_HEADER(PlatformCompilerPreSetup.h)

// 2. 然后包含通用编译器预设
#include "GenericPlatform/GenericPlatformCompilerPreSetup.h"

// 3. 基于预定义宏检测 CPU 架构
#ifndef PLATFORM_CPU_X86_FAMILY
    #if (defined(_M_IX86) || defined(__i386__) || defined(_M_X64) ...)
        #define PLATFORM_CPU_X86_FAMILY 1
    #else
        #define PLATFORM_CPU_X86_FAMILY 0
    #endif
#endif
```

流程说明：
1. UBT 在编译时通过 `-DCOMPILED_PLATFORM_HEADER=Windows/WindowsPlatform.h` 传入平台头文件路径。
2. `Platform.h` 先统一将所有未定义的平台宏置为 `0`，避免 `#ifdef` 的误用。
3. 再依次检测编译器（Clang / MSVC）、CPU 架构（x86 / ARM）、SIMD 能力（SSE4.2 / AVX / NEON）。
4. 最后包含平台主头文件 `COMPILED_PLATFORM_HEADER(Platform.h)` 完成最终宏定义。

### 优化控制宏的行为链

> 文件：`Engine/Source/Runtime/Core/Public/Misc/CoreMiscDefines.h`，第 43~70 行

```cpp
#define UE_DISABLE_OPTIMIZATION_SHIP  PRAGMA_DISABLE_OPTIMIZATION_ACTUAL
#if UE_BUILD_DEBUG
    #define UE_ENABLE_OPTIMIZATION_SHIP  PRAGMA_DISABLE_OPTIMIZATION_ACTUAL
#else
    #define UE_ENABLE_OPTIMIZATION_SHIP  PRAGMA_ENABLE_OPTIMIZATION_ACTUAL
#endif
```

`UE_DISABLE_OPTIMIZATION` 宏在 Debug 配置下禁用优化，在 Shipping 配置下则会被构建系统拦截（通过 `UE_CHECK_DISABLE_OPTIMIZATION` 静态断言防止误提交）。

---

## 与上下层的关系

### 上层调用者

- **所有 Runtime/Editor 模块**：每个 `.cpp` 文件几乎都以 `#include "CoreMinimal.h"` 开头。
- **UHT 生成的代码**：`.generated.h` 依赖 `CoreTypes.h` 中的 `int32`、`FName` 等类型。

### 下层依赖

- **操作系统 SDK**：`HAL/Platform.h` 最终包含 `Windows.h` 或 `unistd.h`。
- **编译器内置特性**：如 `__has_cpp_attribute`、`__COUNTER__`。

---

## 设计亮点与可迁移经验

1. **统一类型别名隔离平台差异**：不直接使用 `int`、`long`，而是定义 `int32`/`int64`，避免 32/64 位平台类型长度不一致的问题。
2. **宏预置为零模式**：所有 `PLATFORM_XXX` 宏先统一设为 `0`，再用平台头文件覆盖。这消除了 `#ifdef` 的隐式风险，强制使用 `#if PLATFORM_WINDOWS`。
3. **最小头文件策略**：`CoreMinimal.h`  curated（策展式）包含最常用的 100 个头文件，既保证编译速度，又减少模块开发者的选择负担。
4. **编译期弃用与优化控制**：`UE_DEPRECATED` 和 `UE_DISABLE_OPTIMIZATION` 提供了工程化的大规模代码库演进手段。

---

## 关键源码片段

> 文件：`Engine/Source/Runtime/Core/Public/HAL/Platform.h`，第 183~210 行

```cpp
// Defines for the availibility of the various levels of vector intrinsics.
#ifndef PLATFORM_ENABLE_VECTORINTRINSICS
    #define PLATFORM_ENABLE_VECTORINTRINSICS 0
#endif

// UE5.2+ requires SSE4.2.
#ifndef PLATFORM_ALWAYS_HAS_SSE4_2
    #define PLATFORM_ALWAYS_HAS_SSE4_2 PLATFORM_CPU_X86_FAMILY
#endif
```

> 文件：`Engine/Source/Runtime/Core/Public/CoreMinimal.h`，第 22~100 行

```cpp
/*----------------------------------------------------------------------------
    Commonly used headers
----------------------------------------------------------------------------*/
#include "Misc/VarArgs.h"
#include "Logging/LogVerbosity.h"
#include "Misc/OutputDevice.h"
#include "HAL/PlatformCrt.h"
// ... (约 100 个头文件聚合)
```

---

## 关联阅读

- [[UE-Core-源码解析：字符串与容器]]
- [[UE-Core-源码解析：数学库与 SIMD]]
- [[UE-Core-源码解析：日志、断言与调试]]
- [[UE-构建系统-源码解析：模块依赖与 Build.cs]]

---

## 索引状态

- **所属阶段**：第二阶段 — 基础层源码解析 / 2.1 Core 基础类型与工具
- **对应笔记**：UE-Core-源码解析：基础类型与宏体系
- **本轮完成度**：✅ 第三轮（接口层 + 数据层 + 逻辑层）
- **更新日期**：2026-04-17

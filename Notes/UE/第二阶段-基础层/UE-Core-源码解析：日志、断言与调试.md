---
title: UE-Core-源码解析：日志、断言与调试
date: 2026-04-17
tags:
  - ue-source
  - engine-architecture
aliases:
  - UE-Core 日志断言与调试
---

> [← 返回 UE全解析主索引]([[00-UE全解析主索引|UE全解析主索引]])

# UE-Core-源码解析：日志、断言与调试

## 模块定位

- **UE 模块路径**：`Engine/Source/Runtime/Core/`
- **核心目录**：`Public/Logging/`、`Public/Misc/`、`Public/HAL/PlatformCrashContext.h`
- **核心头文件**：`Logging/LogMacros.h`、`Logging/LogCategory.h`、`Misc/AssertionMacros.h`
- **模块角色**：提供 UE 全引擎统一的日志输出、断言校验、崩溃上下文收集与调试诊断基础设施。

---

## 接口梳理（第 1 层）

### 日志子系统头文件

| 头文件 | 核心类/宏 | 职责 |
|--------|----------|------|
| `LogVerbosity.h` | `ELogVerbosity::Type` | 日志级别枚举（Fatal/Error/Warning/Display/Log/Verbose/VeryVerbose） |
| `LogCategory.h` | `FLogCategoryBase`、`FLogCategory<>` | 日志分类与动态过滤 |
| `LogMacros.h` | `UE_LOG`、`UE_CLOG` | 日志记录宏 |
| `OutputDevice.h` | `FOutputDevice` | 日志输出设备抽象（文件、控制台、Debugger） |

### 断言子系统头文件

| 头文件 | 核心类/宏 | 职责 |
|--------|----------|------|
| `AssertionMacros.h` | `check`、`verify`、`ensure` 家族 | 运行时断言与校验 |
| `AssertionMacros.h` | `FDebug` | 断言失败处理与堆栈转储 |

### 关键宏接口

> 文件：`Engine/Source/Runtime/Core/Public/Logging/LogMacros.h`，第 270~300 行

```cpp
#define UE_LOG(CategoryName, Verbosity, Format, ...) \
    UE_PRIVATE_LOG(UE_EMPTY, constexpr, CategoryName, Verbosity, Format, ##__VA_ARGS__)

#define UE_CLOG(Condition, CategoryName, Verbosity, Format, ...) \
    UE_PRIVATE_LOG(if (Condition), constexpr, CategoryName, Verbosity, Format, ##__VA_ARGS__)
```

> 文件：`Engine/Source/Runtime/Core/Public/Misc/AssertionMacros.h`，第 228~248 行

```cpp
#ifndef check
    #define check(expr) UE_CHECK_IMPL(expr)
#endif
#ifndef verify
    #define verify(expr) UE_CHECK_IMPL(expr)
#endif

#define UE_CHECK_IMPL(expr) \
    { \
        if(UNLIKELY(!(expr))) \
        { \
            if (FDebug::CheckVerifyFailedImpl2(#expr, __FILE__, __LINE__, TEXT(""))) \
            { \
                PLATFORM_BREAK(); \
            } \
            CA_ASSUME(false); \
        } \
    }
```

---

## 数据结构（第 2 层）

### 日志级别（ELogVerbosity）

```cpp
namespace ELogVerbosity
{
    enum Type : uint8
    {
        NoLogging    = 0,
        Fatal        = 1,
        Error        = 2,
        Warning      = 3,
        Display      = 4,
        Log          = 5,
        Verbose      = 6,
        VeryVerbose  = 7,
        NumVerbosity = 8,
        VerbosityMask = 0xF,
        SetColor     = 0x40,
        BreakOnLog   = 0x80,
    };
}
```

- **CompileTimeVerbosity**：编译期裁剪级别，低于此级别的日志在编译时即被删除。
- **Runtime Verbosity**：通过 `FLogCategoryBase::Verbosity` 动态调整。

### FLogCategoryBase 的内存布局

> 文件：`Engine/Source/Runtime/Core/Public/Logging/LogCategory.h`，第 20~81 行

```cpp
struct FLogCategoryBase
{
    ELogVerbosity::Type Verbosity;           // 当前运行时节流级别
    bool                DebugBreakOnLog;     // 是否在输出时触发断点
    uint8               DefaultVerbosity;    // 启动默认值
    const ELogVerbosity::Type CompileTimeVerbosity; // 编译期裁剪上限
    const FLogCategoryName CategoryName;     // 分类名称（FName/FLazyName）
};
```

### 断言体系的三级分类

| 类型 | 表达式是否总是执行 | 失败时行为 | 适用场景 |
|------|------------------|-----------|---------|
| `check` / `checkf` | 否（仅在 `DO_CHECK` 开启时编译） | `PLATFORM_BREAK()` | 内部不变量 |
| `verify` / `verifyf` | 是 | `DO_CHECK` 开启时断点，否则仅返回值 | 必须有副作用的表达式 |
| `ensure` / `ensureMsgf` | 是 | 记录错误+堆栈，首次失败时断点（非 fatal） | 容错恢复路径 |

### FDebug 的冷路径分离

> 文件：`Engine/Source/Runtime/Core/Public/Misc/AssertionMacros.h`，第 16~34 行

```cpp
#ifndef UE_DEBUG_SECTION
#if (DO_CHECK || DO_GUARD_SLOW || DO_ENSURE) && !PLATFORM_CPU_ARM_FAMILY
    #define UE_DEBUG_SECTION PLATFORM_CODE_SECTION(".uedbg")
#else
    #define UE_DEBUG_SECTION
#endif
#endif
```

将断言实现代码放入独立的 `.uedbg` section，使其远离热路径指令缓存，减少分支预测污染。ARM 平台因分支范围限制不使用此策略。

---

## 行为分析（第 3 层）

### UE_LOG 的调用链

1. **宏展开**：`UE_LOG(LogCore, Log, TEXT("Hello %s"), Name)`
2. **编译期过滤**：检查 `ELogVerbosity::Log <= CompileTimeVerbosity`，否则宏体为空。
3. **运行时过滤**：调用 `Category.IsSuppressed(Verbosity)`，若被抑制则直接返回。
4. **日志分发**：构造 `FStaticBasicLogRecord`，传入 `UE::Logging::Private::BasicLog`。
5. **输出设备遍历**：`FOutputDevice` 派生类（`FOutputDeviceFile`、`FOutputDeviceConsole`、`FOutputDeviceDebug`）依次接收格式化字符串。

> 文件：`Engine/Source/Runtime/Core/Public/Logging/LogMacros.h`，第 117~175 行

```cpp
namespace UE::Logging::Private
{
    struct FStaticBasicLogRecord
    {
        const TCHAR* Format = nullptr;
        const ANSICHAR* File = nullptr;
        int32 Line = 0;
        #if LOGTRACE_ENABLED
        FStaticBasicLogDynamicData& DynamicData;
        #endif
    };

    template <ELogVerbosity::Type Verbosity>
    CORE_API void BasicLog(FStaticBasicLogRecordParam Log, const FLogCategoryBase* Category, ...);
}
```

`FStaticBasicLogRecord` 使用 `static` 存储期，避免每次日志调用都重新构造记录对象。

### check vs verify 的行为差异

```cpp
// check：表达式仅在 DO_CHECK=1 时存在
#if DO_CHECK
    #define check(expr) UE_CHECK_IMPL(expr)
#else
    #define check(expr)  // 完全消失
#endif

// verify：表达式始终存在，但失败处理只在 DO_CHECK 时生效
#if DO_CHECK
    #define verify(expr) UE_CHECK_IMPL(expr)
#else
    #define verify(expr) (expr)
#endif
```

**关键区别**：若表达式包含副作用（如指针赋值），必须使用 `verify` 或 `ensure`，否则 Shipping 构建中该副作用会丢失。

### Ensure 的"首次失败断点"机制

> 文件：`Engine/Source/Runtime/Core/Public/Misc/AssertionMacros.h`，第 119~154 行

```cpp
static CORE_API void EnsureFailed(
    const ANSICHAR* Expr, const ANSICHAR* File, int32 Line, 
    void* ProgramCounter, const TCHAR* Msg );
```

`EnsureFailed` 被 CrashReporter 识别（函数名以 `Ensure` 开头），行为流程：
1. 收集调用堆栈。
2. 生成 ensure 报告（类似 mini-dump）。
3. 若 `GIgnoreDebugger` 为 false，触发 `PLATFORM_BREAK()`。
4. 记录失败次数，后续相同 ensure 不再断点，避免死循环。

---

## 与上下层的关系

### 上层调用者

- **所有引擎模块**：`UE_LOG(LogXXX, ...)` 无处不在。
- **CrashReporter**：读取 ensure 失败记录与堆栈信息。
- **编辑器**：`OutputDevice` 将日志输出到 Editor 的 Output Log 窗口。

### 下层依赖

- **平台调试 API**：`PLATFORM_BREAK()` 在 Windows 下映射为 `__debugbreak()`，在 Linux 下为 `raise(SIGTRAP)`。
- **文件系统**：`FOutputDeviceFile` 将日志写入 `Saved/Logs/`。
- **TraceLog 模块**：通过 `LOGTRACE_ENABLED` 将日志事件接入 UnrealInsights。

---

## 设计亮点与可迁移经验

1. **编译期 + 运行时的双层过滤**：`CompileTimeVerbosity` 在 Shipping 构建中完全剔除 Verbose 日志，运行时 Verbosity 允许动态开启 Debug 信息，兼顾性能与可调试性。
2. **check / verify / ensure 的三级断言语义**：明确区分"内部不变量"、"带副作用的校验"、"可恢复的容错路径"，避免 Shipping 构建中意外移除关键代码。
3. **冷路径代码段分离（`.uedbg`）**：将断言处理逻辑隔离到独立的 ELF/PE section，减少 I-cache 污染，是对大规模代码库性能敏感区域的有效优化。
4. **静态日志记录对象**：`FStaticBasicLogRecord` 的 static 存储避免每次日志调用都构造元数据，降低高频日志路径的开销。

---

## 关键源码片段

> 文件：`Engine/Source/Runtime/Core/Public/Logging/LogCategory.h`，第 44~81 行

```cpp
struct FLogCategoryBase
{
    UE_FORCEINLINE_HINT constexpr bool IsSuppressed(ELogVerbosity::Type VerbosityLevel) const
    {
        return !((VerbosityLevel & ELogVerbosity::VerbosityMask) <= Verbosity);
    }

    inline constexpr ELogVerbosity::Type GetVerbosity() const 
    { 
        return (ELogVerbosity::Type)Verbosity; 
    }

    UE_API void SetVerbosity(ELogVerbosity::Type Verbosity);

private:
    ELogVerbosity::Type Verbosity;
    bool DebugBreakOnLog;
    uint8 DefaultVerbosity;
    const ELogVerbosity::Type CompileTimeVerbosity;
    const FLogCategoryName CategoryName;
};
```

> 文件：`Engine/Source/Runtime/Core/Public/Misc/AssertionMacros.h`，第 36~60 行

```cpp
struct FEnsureHandlerArgs
{
    const ANSICHAR* Expression;
    const TCHAR* Message;
};

CORE_API TFunction<bool(const FEnsureHandlerArgs& Args)> SetEnsureHandler(
    TFunction<bool(const FEnsureHandlerArgs& Args)> EnsureHandler);
```

---

## 关联阅读

- [[UE-Core-源码解析：基础类型与宏体系]]
- [[UE-Core-源码解析：字符串与容器]]
- [[UE-TraceLog-源码解析：Tracing 与性能埋点]]

---

## 索引状态

- **所属阶段**：第二阶段 — 基础层源码解析 / 2.1 Core 基础类型与工具
- **对应笔记**：UE-Core-源码解析：日志、断言与调试
- **本轮完成度**：✅ 第三轮（接口层 + 数据层 + 逻辑层）
- **更新日期**：2026-04-17

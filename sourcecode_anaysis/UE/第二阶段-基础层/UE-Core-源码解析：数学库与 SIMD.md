---
title: UE-Core-源码解析：数学库与 SIMD
date: 2026-04-17
tags:
  - ue-source
  - engine-architecture
aliases:
  - UE-Core 数学库与 SIMD
---

> [[00-UE全解析主索引|← 返回 UE全解析主索引]]

# UE-Core-源码解析：数学库与 SIMD

## 模块定位

- **UE 模块路径**：`Engine/Source/Runtime/Core/Public/Math/`
- **核心头文件**：`Vector.h`、`VectorRegister.h`、`Transform.h`、`UnrealMathUtility.h`
- **模块角色**：为引擎、游戏逻辑、渲染、物理提供统一的数学原语与 SIMD 抽象。

---

## 接口梳理（第 1 层）

### 核心数学类型

| 头文件 | 模板/类型 | 职责 |
|--------|----------|------|
| `Vector.h` | `UE::Math::TVector<T>` | 3D 向量（`float`/`double` 特化） |
| `Vector2D.h` | `UE::Math::TVector2<T>` | 2D 向量 |
| `Vector4.h` | `UE::Math::TVector4<T>` | 4D 向量 / SIMD 对齐存储 |
| `Quat.h` | `UE::Math::TQuat<T>` | 四元数 |
| `Rotator.h` | `UE::Math::TRotator<T>` | 欧拉角旋转 |
| `Transform.h` | `UE::Math::TTransform<T>` | 缩放-旋转-位移（SoA 布局） |
| `Matrix.h` | `UE::Math::TMatrix<T>` | 4×4 矩阵 |

### SIMD 抽象层

| 头文件 | 核心概念 | 职责 |
|--------|---------|------|
| `VectorRegister.h` | `VectorRegister4Float`、`VectorRegister4Double` | 跨平台 SIMD 寄存器别名 |
| `UnrealMathSSE.h` | SSE/AVX 指令封装 | x86 平台实现 |
| `UnrealMathNeon.h` | NEON 指令封装 | ARM 平台实现 |
| `UnrealMathFPU.h` | 标量回退实现 | 无 SIMD 平台 |

> 文件：`Engine/Source/Runtime/Core/Public/Math/VectorRegister.h`，第 10~20 行

```cpp
#if PLATFORM_ENABLE_VECTORINTRINSICS_NEON
#include "Math/UnrealMathNeon.h"
#elif defined(__cplusplus_cli)
#include "Math/UnrealMathFPU.h"
#elif PLATFORM_ENABLE_VECTORINTRINSICS
#include "Math/UnrealMathSSE.h"
#else
#include "Math/UnrealMathFPU.h"
#endif
```

---

## 数据结构（第 2 层）

### TVector 的内存布局

> 文件：`Engine/Source/Runtime/Core/Public/Math/Vector.h`，第 49~77 行

```cpp
template<typename T>
struct TVector
{
    static_assert(std::is_floating_point_v<T>, "T must be floating point");

    union
    {
        struct { T X; T Y; T Z; };
        UE_DEPRECATED(all, "For internal use only")
        T XYZ[3];
    };

    static constexpr int32 NumComponents = 3;
};
```

- **AoS（Array of Structs）布局**：`X`、`Y`、`Z` 连续存储。
- **默认无初始化**：构造器不置零，避免不必要的内存写入。
- **NaN 诊断**：在 `ENABLE_NAN_DIAGNOSTIC` 开启时，运算后会自动检查 `ContainsNaN()`。

### TTransform 的 SoA 布局

`TTransform` 是 UE 中最重要的数学结构之一，采用**结构体数组（Structure of Arrays）**思想：

```cpp
// 概念布局
template<typename T>
struct TTransform
{
    TVector<T>  Translation;    // 位移
    TQuat<T>    Rotation;       // 旋转（四元数）
    TVector<T>  Scale3D;        // 缩放
};
```

虽然表面上是 AoS，但在批量动画评估、骨骼变换传播等场景中，UE 会在上层将 `TTransform` 数组拆分为 `Translations[]`、`Rotations[]`、`Scales[]` 进行 SIMD 批量处理。

### SIMD 寄存器类型映射

> 文件：`Engine/Source/Runtime/Core/Public/Math/VectorRegister.h`，第 25~50 行

```cpp
namespace UE::Math::VectorRegisterPrivate
{
    template<> struct TVectorRegisterTypeHelper<float>  { using Type = VectorRegister4Float; };
    template<> struct TVectorRegisterTypeHelper<double> { using Type = VectorRegister4Double; };
}

template<typename T>
using TVectorRegisterType = typename UE::Math::VectorRegisterPrivate::TVectorRegisterTypeHelper<T>::Type;
```

在 x86 SSE 平台：
- `VectorRegister4Float` = `__m128`
- `VectorRegister4Double` = `__m256d`（若启用 AVX）或 `__m128d`（SSE2）

在 ARM NEON 平台：
- `VectorRegister4Float` = `float32x4_t`

---

## 行为分析（第 3 层）

### SIMD 选择流程（编译期多态）

1. `Platform.h` 检测平台能力，定义 `PLATFORM_ENABLE_VECTORINTRINSICS` 和 `PLATFORM_ENABLE_VECTORINTRINSICS_NEON`。
2. `VectorRegister.h` 通过条件编译包含对应的平台实现头文件。
3. 上层代码使用统一的跨平台函数（如 `VectorAdd`、`VectorDot4`），编译期展开为对应的 SIMD 指令。

> 文件：`Engine/Source/Runtime/Core/Public/HAL/Platform.h`，第 183~210 行

```cpp
#ifndef PLATFORM_ENABLE_VECTORINTRINSICS
    #define PLATFORM_ENABLE_VECTORINTRINSICS 0
#endif

#ifndef PLATFORM_ALWAYS_HAS_SSE4_2
    #define PLATFORM_ALWAYS_HAS_SSE4_2 PLATFORM_CPU_X86_FAMILY
#endif
```

UE 5.2+ 在 x86 平台上**强制要求 SSE4.2**，这意味着不需要运行时 CPUID 检测即可使用 `_mm_cmpistrm` 等字符串处理指令。

### TVector 的关键运算路径

以 `DotProduct` 为例：

```cpp
// Vector.h 中声明
[[nodiscard]] UE_FORCEINLINE_HINT static T DotProduct(const TVector<T>& A, const TVector<T>& B);
```

对于标量版本（FPU）：
- 直接计算 `A.X*B.X + A.Y*B.Y + A.Z*B.Z`。

对于批量 SIMD 版本（如动画评估）：
- 上层会将 4 个 `TVector` 的 X 分量打包到一个 `__m128`。
- 调用 `VectorDot4` 一次完成 4 个点积。

### PersistentVectorRegister 的内存对齐策略

> 文件：`Engine/Source/Runtime/Core/Public/Math/VectorRegister.h`，第 55~116 行

```cpp
#if !defined(UE_SSE_DOUBLE_ALIGNMENT) || (UE_SSE_DOUBLE_ALIGNMENT <= 16)
    using PersistentVectorRegister4Double = VectorRegister4Double;
#else
    struct alignas(16) PersistentVectorRegister4Double
    {
        double XYZW[4];
        // ... 显式转换操作符
    };
#endif
```

**设计意图**：
- AVX 的 `__m256d` 需要 32 字节对齐，但某些分配器对大对齐要求的类型会产生显著内存开销。
- `PersistentVectorRegister4Double` 强制 16 字节对齐存储，在加载/存储时使用未对齐指令（现代 CPU 上代价极低），换取内存布局的紧凑性。

---

## 与上下层的关系

### 上层调用者

- **渲染器（Renderer）**：`FMatrix`、`FVector` 是视图/投影矩阵计算的基石。
- **动画系统（AnimationCore）**：骨骼变换批量评估大量使用 `TTransform` 和 `VectorRegister`。
- **物理系统（Chaos）**：碰撞检测、刚体积分使用 `FVector` / `FQuat`。

### 下层依赖

- **编译器 SIMD 内建函数**：`__m128`（MSVC/Clang）、`float32x4_t`（ARM Clang）。
- **平台对齐支持**：`alignas` 依赖 C++11 和平台 ABI 支持。

---

## 设计亮点与可迁移经验

1. **模板化浮点精度**：`TVector<float>`（`FVector`）与 `TVector<double>`（`FVector3d`）共用一份模板代码，方便大坐标世界和物理高精度需求的切换。
2. **编译期 SIMD 后端切换**：不依赖运行时检测，减少分支和函数指针开销；Shipping 构建中所有 SIMD 调用都是直接内联的。
3. **PersistentVectorRegister 的对齐折中**：在 SIMD 性能与内存分配开销之间提供了显式的权衡方案，避免了盲目追求最大对齐导致的内存膨胀。
4. **NaN 诊断的可开关性**：`ENABLE_NAN_DIAGNOSTIC` 在 Development 构建中自动捕获浮点异常并归零，防止 NaN 污染传播到渲染管线。

---

## 关键源码片段

> 文件：`Engine/Source/Runtime/Core/Public/Math/Vector.h`，第 240~266 行（概念引用）

```cpp
[[nodiscard]] UE_FORCEINLINE_HINT TVector<T> Cross(const TVector<T>& V2) const;
[[nodiscard]] UE_FORCEINLINE_HINT static TVector<T> CrossProduct(const TVector<T>& A, const TVector<T>& B);
[[nodiscard]] UE_FORCEINLINE_HINT T Dot(const TVector<T>& V) const;
[[nodiscard]] UE_FORCEINLINE_HINT static T DotProduct(const TVector<T>& A, const TVector<T>& B);
```

> 文件：`Engine/Source/Runtime/Core/Public/Math/VectorRegister.h`，第 10~20 行

```cpp
#if PLATFORM_ENABLE_VECTORINTRINSICS_NEON
#include "Math/UnrealMathNeon.h"
#elif PLATFORM_ENABLE_VECTORINTRINSICS
#include "Math/UnrealMathSSE.h"
#else
#include "Math/UnrealMathFPU.h"
#endif
```

---

## 关联阅读

- [[UE-Core-源码解析：基础类型与宏体系]]
- [[UE-Engine-源码解析：场景图与变换传播]]
- [[UE-Engine-源码解析：Tick 调度与分阶段更新]]

---

## 索引状态

- **所属阶段**：第二阶段 — 基础层源码解析 / 2.1 Core 基础类型与工具
- **对应笔记**：UE-Core-源码解析：数学库与 SIMD
- **本轮完成度**：✅ 第三轮（接口层 + 数据层 + 逻辑层）
- **更新日期**：2026-04-17

---
title: CPU缓存体系
date: 2026-04-18
tags:
  - 操作系统
  - CPU
  - Cache
  - 性能优化
aliases:
  - CPU Cache Hierarchy
---

> [← 返回 CPU与内存架构索引]([[索引\|CPU与内存架构索引]])

# CPU 缓存体系

---

## Why：为什么要学习 CPU 缓存？

- **性能鸿沟**：现代 CPU 主频 3GHz+，但主存延迟高达 100ns，相当于 CPU 空转数百个周期。
- **帧率不稳定**：游戏引擎中一次 cache miss 可能导致微卡顿；大量 miss 则直接掉帧。
- **数据导向设计（DOD）的基础**：ECS、SoA（Structure of Arrays）等引擎架构都是为了配合缓存体系而生。

---

## What：CPU 缓存是什么？

### 缓存层级

| 层级 | 典型大小 | 典型延迟 | 所属 |
|------|---------|----------|------|
| L1i / L1d | 32KB + 32KB | ~1ns (3-4 cycles) | 每核心独占 |
| L2 | 256KB - 1MB | ~4ns (10-12 cycles) | 每核心独占 |
| L3 (LLC) | 8MB - 64MB | ~40ns (40-50 cycles) | 多核心共享 |
| 主存 | 8GB+ | ~100ns+ | 全局 |

### 核心概念

- **Cache Line**：缓存与主存传输的最小单位，通常为 **64 字节**（x86_64）。
- **Temporal Locality（时间局部性）**：最近访问的数据很可能再次被访问。
- **Spatial Locality（空间局部性）**：访问某地址后，邻近地址很可能被访问。
- **Cache Miss 类型**：
  - **Compulsory**：首次访问，必然 miss。
  - **Capacity**：工作集超过缓存容量。
  - **Conflict**：映射到同一 cache set 导致驱逐。

---

## How：如何优化缓存利用？

### 1. 优先使用连续内存结构
- `std::vector` 优于 `std::list`，数组优于链表。
- 游戏引擎中的粒子系统通常用 SoA（Structure of Arrays）而非 AoS（Array of Structures）。

### 2. 缩小工作集
- 一次处理的数据尽量能放进 L1/L2。
- 分块（Tiling/Blocking）处理大数据集，如矩阵乘法。

### 3. 预取友好
- 顺序访问让硬件预取器（Hardware Prefetcher）工作。
- 避免随机指针跳转。

---

## C++ 本地验证实验

### 实验 1：数组 vs 链表遍历

```cpp
// workspace/os-lab/cpu-cache/array_vs_list.cpp
#include <vector>
#include <list>
#include <random>
#include <chrono>
#include <iostream>

constexpr size_t N = 100'000;
constexpr int ITER = 1000;

int main() {
    // 1. 连续数组
    std::vector<int> vec(N);
    for (size_t i = 0; i < N; ++i) vec[i] = static_cast<int>(i);

    auto t1 = std::chrono::high_resolution_clock::now();
    volatile int sum = 0;
    for (int it = 0; it < ITER; ++it) {
        for (size_t i = 0; i < N; ++i) {
            sum += vec[i];
        }
    }
    auto t2 = std::chrono::high_resolution_clock::now();

    // 2. 链表（模拟指针跳跃）
    std::list<int> lst(vec.begin(), vec.end());
    auto t3 = std::chrono::high_resolution_clock::now();
    for (int it = 0; it < ITER; ++it) {
        for (auto& v : lst) {
            sum += v;
        }
    }
    auto t4 = std::chrono::high_resolution_clock::now();

    auto ms_vec = std::chrono::duration<double, std::milli>(t2 - t1).count();
    auto ms_lst = std::chrono::duration<double, std::milli>(t4 - t3).count();

    std::cout << "Vector: " << ms_vec << " ms\n";
    std::cout << "List  : " << ms_lst << " ms\n";
    std::cout << "Ratio : " << ms_lst / ms_vec << "x\n";
    return 0;
}
```

**预期结果**：数组遍历通常比链表快 **5-20 倍**，核心差异来自 cache miss 数量。

### 实验 2：行优先 vs 列优先（矩阵遍历）

```cpp
// workspace/os-lab/cpu-cache/row_vs_col.cpp
#include <vector>
#include <chrono>
#include <iostream>

constexpr int N = 2048;

int main() {
    std::vector<std::vector<int>> mat(N, std::vector<int>(N, 1));
    volatile int sum = 0;

    // 行优先 — 缓存友好
    auto t1 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j)
            sum += mat[i][j];
    auto t2 = std::chrono::high_resolution_clock::now();

    // 列优先 — 缓存不友好
    auto t3 = std::chrono::high_resolution_clock::now();
    for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i)
            sum += mat[i][j];
    auto t4 = std::chrono::high_resolution_clock::now();

    std::cout << "Row-major: "
              << std::chrono::duration<double, std::milli>(t2 - t1).count()
              << " ms\n";
    std::cout << "Col-major: "
              << std::chrono::duration<double, std::milli>(t4 - t3).count()
              << " ms\n";
    return 0;
}
```

**预期结果**：行优先通常快 **3-10 倍**。

---

## 游戏引擎延伸

### ECS 与 SoA

- **传统 OOP（AoS）**：`std::vector<Transform>` 每个对象包含位置/旋转/缩放，但更新时往往只读位置。
- **SoA**：`Positions[]`, `Velocities[]`, `Healths[]` 分开存储，一次遍历只加载需要的字段，减少无效数据占满 Cache Line。
- 实际案例：Unity DOTS、UE Mass Entity 都基于这一思想。

### 渲染剔除

- 视锥剔除、遮挡剔除前先对物体按空间做粗略排序，保证遍历时的空间局部性。
- 提交渲染命令时，尽量按材质/Shader 排序，避免 CPU 端状态切换带来的缓存抖动。

---

> [!info] 延伸阅读
> - [[内存对齐与填充]] — 了解 Cache Line 边界与 False Sharing
> - [[性能分析与工具/Cache-Miss分析实战]] — 用 perf 量化 Cache Miss

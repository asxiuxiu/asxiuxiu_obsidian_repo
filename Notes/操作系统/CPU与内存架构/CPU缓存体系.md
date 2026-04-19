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
- **数据导向设计（DOD，Data-Oriented Design）的基础**：ECS、SoA（Structure of Arrays）等引擎架构都是为了配合缓存体系而生。DOD 的核心理念是"以缓存友好的数据布局为首要目标"，而非面向对象设计中"以行为封装为中心"。

> [!note] 前置知识
> 若对 CPU 流水线、寄存器、ALU、内存层次结构等基础概念尚不熟悉，建议先阅读 [[计算机体系结构速览：CPU、内存与总线]]。


---

## What：CPU 缓存是什么？

### 缓存层级

| 层级 | 典型大小 | 典型延迟 | 所属 |
|------|---------|----------|------|
| L1i / L1d | 32KB + 32KB | ~1ns (3-4 cycles) | 每核心独占 |
| L2 | 256KB - 1MB | ~4ns (10-12 cycles) | 每核心独占 |
| L3 (LLC) | 8MB - 64MB | ~40ns (40-50 cycles) | 多核心共享 |
| 主存 | 8GB+ | ~100ns+ | 全局 |

### 缓存的演进历史

CPU 缓存并非一开始就存在。理解它的演进，才能理解为什么现代缓存体系是这样设计的。

| 年代 | 代表处理器 | 缓存特征 | 演进动因 |
|------|-----------|----------|----------|
| 1980s 早期 | Intel 8086 | **无缓存** | CPU 主频与内存速度基本匹配，直接访问 DRAM 即可 |
| 1980s 中期 | Intel 386/486 | **片外 L1 缓存**（8~32KB） | CPU 主频快速提升，DRAM 跟不上了。在主板上加 SRAM 作为缓冲 |
| 1990s | Pentium / Pentium Pro | **片内 L1（分离 I$ / D$）+ 片外 L2** | 流水线加深，取指和数据访问竞争同一缓存成为瓶颈。分离指令缓存（I-Cache）和数据缓存（D-Cache）解决结构冒险 |
| 2000s | Pentium 4 / Core 2 | **片内 L1 + 片内 L2 + 片外/封装内 L3** | 多核时代到来，私有 L2 导致核心间共享数据效率低。L3 作为共享缓存减少跨核通信 |
| 2010s+ | Core iX / Ryzen | **片内 L1/L2/L3，L3 成为主要战场** | 主频触及功耗墙（Power Wall），提升单核性能转向提升缓存容量和智能预取 |

> **核心洞察**：缓存的演进是**CPU 速度与内存速度差距不断拉大**的必然结果。每一级缓存的出现，都是因为上一级存储无法满足 CPU 的"饥饿速度"。

### Cache 的组织方式：映射、写策略与替换

缓存不是简单的"一块高速内存"，它的内部结构决定了程序的行为。

#### 1. 映射方式（Mapping）

主存中的每个块可以放到 Cache 的哪个位置？有三种策略：

| 映射方式 | 原理 | 优点 | 缺点 | 应用 |
|----------|------|------|------|------|
| **直接映射（Direct Mapped）** | 每个主存块只能放到唯一确定的 Cache 行 | 实现简单，查找只需比较一次 Tag | 易发生 Conflict Miss（不同块争抢同一行） | 早期 CPU、嵌入式 |
| **全相联（Fully Associative）** | 每个主存块可以放到任意 Cache 行 | 无 Conflict Miss | 查找需同时比较所有行，硬件成本高、速度慢 | TLB、小容量缓存 |
| **组相联（Set Associative）** | Cache 分成若干组，每组内有 N 个行（N-way）。主存块映射到组，组内任意放 | 平衡了直接映射和全相联 | 复杂度介于两者之间 | **现代 CPU 主流**（如 8-way L1、16-way L3） |

> 现代 CPU 的 L1 缓存通常是 **4-way 或 8-way 组相联**。这意味着一个主存块有 4~8 个"候选位置"，大大减少了 Conflict Miss，同时硬件成本可控。

#### 2. 写策略（Write Policy）

CPU 写入缓存时，如何保持缓存与主存的一致性？

| 策略 | 原理 | 优点 | 缺点 | 应用 |
|------|------|------|------|------|
| **Write-Through（直写）** | 同时写缓存和主存 | 主存始终最新，实现简单 | 每次写都要访问慢速主存，性能差 | 早期系统、简单嵌入式 |
| **Write-Back（回写）** | 只写缓存，被替换时才写回主存。每行增加一个 **Dirty 位** 标记是否被修改 | 写操作速度快，减少总线带宽占用 | 主存可能过时，需复杂的缓存一致性协议保证多核一致 | **现代 CPU 主流** |

> 游戏引擎开发者需注意：Write-Back 策略意味着"数据写到指针地址"不等于"数据到达主存"。多线程间通过共享内存同步时，必须显式使用内存屏障（`memory_order_release` / `sfence`）刷 Store Buffer 和 Cache。

#### 3. 替换算法（Replacement Policy）

当组内所有行都被占满，又需要加载新块时，替换哪一行？

- **LRU（Least Recently Used，最近最少使用）**：替换最久未被访问的行。最符合局部性原理，现代 CPU 常用近似 LRU（Pseudo-LRU）。
- **LFU（Least Frequently Used，最少使用）**：替换访问次数最少的行。实现复杂，实际较少使用。
- **Random（随机）**：硬件最简单，但性能不如 LRU。

> **对引擎优化的启示**：若你的数据访问模式是"顺序遍历大数组"，LRU 是完美的——每次新加载的块都会把最旧的块替换掉。但若你的模式是"反复访问两个超过缓存容量的工作集"（如同时处理两张超大纹理），就会触发**抖动（Thrashing）**——缓存不断加载 A 驱逐 B，然后加载 B 驱逐 A，命中率趋近于零。

### 核心概念

- **Cache Line**：缓存与主存传输的最小单位，通常为 **64 字节**（x86_64）。

> **为什么是 64 字节？** 这是**空间局部性**与**总线效率**的权衡：
> - **更大（如 128B）**：一次加载覆盖更多邻近数据，空间局部性好；但一次加载时间更长，且更容易触发 False Sharing（多核争抢同一行）。
> - **更小（如 32B）**：加载快，False Sharing 概率低；但总线事务更频繁，空间局部性利用不足。
> - **64B 成为行业标准**，因为它在大多数工作负载下取得了最佳平衡。ARM 部分架构使用 128B Cache Line，这也是为什么 ARM 服务器上 False Sharing 问题更隐蔽。

- **Temporal Locality（时间局部性）**：最近访问的数据很可能再次被访问。
- **Spatial Locality（空间局部性）**：访问某地址后，邻近地址很可能被访问。
- **Cache Miss 类型**：
  - **Compulsory**：首次访问，必然 miss。可通过**预取（Prefetching）**缓解。
  - **Capacity**：工作集超过缓存容量。只能通过**缩小工作集**或**分块处理（Tiling/Blocking）**解决。
  - **Conflict**：映射到同一 cache set 导致驱逐。可通过**数据对齐、改变数组维度**缓解。

---

## How：如何优化缓存利用？

### 1. 优先使用连续内存结构
- `std::vector` 优于 `std::list`，数组优于链表。
- 游戏引擎中的粒子系统通常用 SoA（Structure of Arrays）而非 AoS（Array of Structures）。

```
AoS（面向对象）：              SoA（数据导向）：
┌─────────┬─────────┐         ┌─────────┬─────────┬─────────┐
│ Pos X   │ Pos Y   │         │ Pos X[] │ Pos Y[] │ Vel X[] │ ...
├─────────┼─────────┤         ├─────────┼─────────┼─────────┤
│ Vel X   │ Vel Y   │         │  连续   │  连续   │  连续   │
├─────────┼─────────┤         └─────────┴─────────┴─────────┘
│  ...    │  ...    │         一次加载 Cache Line 全是同类型数据
└─────────┴─────────┘         没有无用字段污染缓存
每个对象字段交错存储
更新位置时把速度也加载进缓存（无用数据）
```

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

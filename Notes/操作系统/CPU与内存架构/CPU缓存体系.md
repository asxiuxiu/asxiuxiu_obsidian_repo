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
| **主存（RAM）** | **8GB ~ 64GB+** | **~100ns+** | **全局，即任务管理器看到的"内存"** |

> [!warning] 常见混淆：CPU 缓存 ≠ 内存（RAM）
>
> 任务管理器显示的"内存占用"（如 16GB/64GB）指的是**主存（RAM）**，而非 CPU 缓存。缓存容量只有 KB~MB 级别，且对操作系统透明，通常不会在任务管理器中显示。
>
> | | CPU 缓存（L1/L2/L3） | 主存 / RAM |
> |---|---|---|
> | **容量** | KB ~ MB 级 | GB 级（8GB、32GB、64GB…） |
> | **位置** | CPU 芯片内部或封装内 | 主板上的内存条 |
> | **延迟** | 1ns ~ 40ns | ~100ns |
> | **谁管理** | CPU 硬件自动管理 | 操作系统分配、任务管理器显示 |
> | **作用** | 缓冲 CPU 与主存的速度差 | 存放运行中的程序与数据 |
>
> 一句话：**内存是"大仓库"，缓存是 CPU 手边的"小操作台"**——两者都是存储，但层级、速度和目的完全不同。

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

### 前置概念：主存分块与 Cache 行

在理解 Cache 如何组织之前，必须先搞清楚两个最基本的问题：**主存数据是怎么搬进 Cache 的？** 以及 **以什么为单位搬？**

#### 主存被切成"块"，Cache 被分成"行"

主存和 Cache 是**两个完全独立的存储空间**，不是谁包含谁。Cache 坐在 CPU 旁边，主存坐在主板上，它们之间靠总线搬运数据。

用物流来类比：

- **主存（内存条 / 64GB）** = 远处的大仓库。仓库里所有货物按固定规格打包成托盘，每托盘 64 件货。这每一托盘就是一个 **"块"（Block）**。
- **Cache（CPU 缓存 / 几 MB）** = 车间门口的小型暂存区。暂存区里的每个货位只能放一托盘货，这个货位就叫 **"行"（Line 或 Row）**。
- **CPU** = 车间里的工人。工人要货时，优先看暂存区（Cache）；没有再去仓库（主存）调。

> 关键规则：**主存的一个块 ↔ Cache 的一行**，大小完全相同（都是 64 字节）。Cache 与主存之间传输数据的最小单位就是一个块。

为什么不是按字节（一件货一件货）搬？因为程序访问内存有**空间局部性**——你读了 `array[i]`，下一行代码很可能读 `array[i+1]`。一次搬一整托盘（64 字节），把邻居也带进来，下次就不用再去仓库取了。

#### 地址的三段拆分

CPU 发出一个内存地址（比如 `0x1234ABCD`），硬件会自动把它拆成三段，用来在 Cache 暂存区里定位：

| 字段             | 作用                       | 类比（物流）             |
| -------------- | ------------------------ | ------------------ |
| **Offset（偏移）** | 定位块内的具体字节（如 64B 块需要 6 位） | 托盘里的第几件货           |
| **Index（索引）**  | 定位 Cache 的哪一组/哪一行        | 暂存区的第几号货位          |
| **Tag（标签）**    | 标识这个块来自主存的哪个"大区"         | 托盘上的仓库编号，防止同货位放错托盘 |

举个例子：Cache 有 1024 行，每行 64 字节。
- 地址的低 6 位 → Offset（在 64 字节里找具体位置）
- 地址的中间 10 位 → Index（在 1024 个行里找对应货位）
- 地址的高位 → Tag（确认这个货位上放的到底是不是我要的那托盘货）

> [!tip] 一句话总结
> **主存分块 → Cache 分行 → 块行大小相同 → 地址拆成 Tag|Index|Offset 来查找**。理解了这一点，下面的"映射方式"就是在回答：当新托盘从仓库运到暂存区时，该放到哪个货位里？

#### 为什么地址非要拆成三段？不能直接用地址去查吗？

这是很多人刚学 Cache 时的核心疑问。问题出在：**主存和 Cache 的容量差距太大**。

假设你的主存是 4GB（32 位地址），Cache 只有 64KB，每行 64B。那么：
- Cache 一共只有 1024 行
- 但主存里有多少个 64B 的块呢？4GB / 64B = 67,108,864 个块

**67,108,864 个主存块，要挤进 1024 个 cache 行里。** 这意味着平均每个 cache 行要"代表" 65536 个不同的主存块。当你拿着一个地址去 cache 找数据时，硬件只能根据地址的 **Index** 位定位到一个 cache 行——但这个行上此刻放的到底是 65536 个可能块中的哪一个？

> **Tag 的唯一作用，就是消除这个歧义。**

打个比方：一个快递柜有 1024 个格子（Index），但全市有 6700 万个包裹（主存块）。包裹按规则只能放在某个固定格子里（由地址 Index 决定）。快递员到格子前，必须核对包裹上的单号（Tag），确认这个格子里放的到底是不是他要找的那个包裹。

| 字段 | 解决的问题 | 没有它会怎样 |
|------|-----------|------------|
| **Offset** | 块内第几个字节 | 找不到具体数据 |
| **Index** | 去哪个 cache 行找 | 要搜遍所有行，硬件不可行 |
| **Tag** | 确认这一行当前存的是不是我想要的主存块 | 同一个行被多个块复用时，无法区分 |

> **Index 解决"去哪找"，Tag 解决"找得对不对"。**

理解了 Tag 的存在理由，再看下面的"映射方式"，本质上就是在回答：**当新托盘运到暂存区时，该按什么规则放到哪个货位里？**

---

### Cache 的组织方式：映射、写策略与替换

缓存不是简单的"一块高速内存"，它的内部结构决定了程序的行为。

#### 1. 映射方式（Mapping）

##### 为什么会有三种映射方式？

理解了 Tag 和 Index，你可能会问：**既然 Index 已经决定了去哪个行，为什么还要有"直接映射"、"组相联"这些花样？直接规定每个地址固定放一行不就好了吗？**

关键原因是：**Index 决定了查哪一行，但 Index 从哪来、怎么算，是可以有不同的策略的。** 不同的策略对应了"硬件成本"和"命中率"之间的不同取舍。

假设 cache 有 4 行，主存有 1024 个块：

- **直接映射**：块 0→行 0，块 1→行 1，块 2→行 2，块 3→行 3，块 4→行 0，块 5→行 1……
  - 问题：如果你程序里交替访问块 0 和块 4（比如两个数组），它们都挤到行 0，导致不停地换入换出——这叫**冲突抖动（Conflict Thrashing）**。

- **全相联**：块 0 和块 4 可以分别放到任意空闲行，没有冲突问题。
  - 问题：CPU 访问地址时，需要同时问所有行"是不是你？"，需要并行比较电路，**硬件太复杂、太慢**。

- **组相联（2 路）**：4 行分成 2 组，每组 2 行。块 0→组 0（行 0 或 1），块 4→组 0（行 0 或 1）。
  - 同一组还是会竞争，但组内有 2 个位置可以选，冲突概率大幅降低，而电路只需要 2 个比较器。

> **映射方式的本质，是一套"硬件查询算法"**：在查找速度、硬件成本、命中率三者之间取最优。现代 CPU 的 L1 通常 4~8 路组相联，L3 路数更多——越靠近 CPU，对延迟越敏感，电路越精简；越远离 CPU，面积预算越多，可以用更复杂的映射提升命中率。

##### 三种映射策略对比

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

#### False Sharing：64B 也会触发，多核会怎么做？

**False Sharing 的本质**：多个 CPU 核心访问的**不同变量**，恰好落在**同一个 Cache Line** 上。只要这些变量被不同核心频繁写，就会触发——64B 只是"行业标准"，不是"避免 False Sharing 的魔法数字"。

假设核心 A 写变量 `x`，核心 B 写变量 `y`，`x` 和 `y` 在同一个 64B Cache Line 内但完全无关，MESI 协议的执行过程如下：

| 步骤 | 核心 A | 核心 B | 总线/L3 |
|------|--------|--------|---------|
| 1 | 写 `x`，Line 状态变为 **M（Modified）** | Line 为 **I（Invalid）** | — |
| 2 | — | 写 `y`，发现为 I，发 **RFO**（请求所有权） | 转发 RFO 给 A |
| 3 | 收到 RFO，Line 写回 L3，本地置 **I** | 等待 | — |
| 4 | Line 为 I | 获得 Line，状态变 **M**，写 `y` | — |
| 5 | 再写 `x`，又要发 RFO…… | 收到 RFO，写回 L3，置 I | ping-pong 循环 |

> [!warning] 性能灾难
> 正常写 L1d 只需 ~4 cycles；False Sharing 下每次写都要触发完整的**跨核心缓存同步事务**（RFO + 写回 + 状态转换），约 **100+ cycles**。两核交替频繁写时，性能可能暴跌 **10~100 倍**。

**关键洞察**：MESI 协议以 **Cache Line 为单位**管理一致性，硬件无法区分"同一行内的不同变量"。A 和 B 明明没有共享任何逻辑数据，但硬件把整个 Line 当成一个"同步单元"来回 ping-pong。

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

### 4. 消除 False Sharing：Cache Line 对齐

当多个线程/核心各自写自己的计数器或状态时，必须确保它们**不在同一个 Cache Line 上**。

**C++ 解法：`alignas` + Padding**

```cpp
// 让每个计数器独占一个 64B Cache Line
struct alignas(64) PaddedCounter {
    std::atomic<long long> value;
    char padding[64 - sizeof(std::atomic<long long>)];
};

PaddedCounter counters[4]; // 4 个核心各用一个
```

`alignas(64)` 确保结构体从 Cache Line 边界开始，`padding` 填满剩余空间防止编译器紧凑排列。C++17 起也可用 `std::hardware_destructive_interference_size` 代替硬编码 64：

```cpp
#include <new>

struct alignas(std::hardware_destructive_interference_size) Counter {
    std::atomic<long long> value;
};
```

> [!tip] 引擎中的典型踩雷点
> - **任务调度器**：工作线程的 task queue head/tail 指针未对齐，多线程偷任务时 False Sharing
> - **性能计数器**：per-thread 的计时/计数数据未按 Cache Line 隔离，profiling 本身污染性能
> - **ECS 并行写入**：Job System 并行更新 Component 时，不同 Entity 的同一字段可能落入同一 Cache Line（详见下文）
> - **原子引用计数**：多线程共享的 `std::shared_ptr` 控制块，引用计数在同一 Cache Line 上被各核频繁修改

#### 深入：ECS SoA 为何也会踩雷？

很多人疑惑：ECS 不是用 SoA（Structure of Arrays）把同一字段（如所有 Entity 的 `Position.x`）紧邻存储吗？这明明是缓存友好的设计，怎么会是雷区？

**关键区别在"单线程遍历" vs "多线程并行写入"：**

| 场景 | SoA 是否缓存友好 | False Sharing 风险 |
|------|----------------|-------------------|
| 单线程顺序遍历 | ✅ 非常友好，预取器高效工作 | ❌ 无 |
| 多线程并行，处理不相邻的数据块 | ✅ 友好 | ❌ 无 |
| 多线程并行，处理相邻/同 Chunk 的数据 | ✅ 友好 | ⚠️ **有！** |

- **单线程顺序遍历**：SoA 是完美的。`positions[]` 数组连续存储，CPU 顺序读取时一个 Cache Line 能装下多个同类型数据，空间局部性被充分利用。
- **多线程并行写入**：Job System 把数组切分给多个线程时，**边界上的相邻 Entity 可能被不同线程同时写入**。如果这两个 Entity 的同一字段恰好落在同一个 Cache Line（64B）内，MESI 协议就会把整行 Cache 在两核之间来回 ping-pong，造成 **False Sharing**。

**具体例子**：假设 `float health[]` 数组（4 字节/元素）。Job System 让 Thread A 写 `health[15]`，Thread B 写 `health[16]`。若数组基地址未按 64B 对齐，`&health[15]` 与 `&health[16]` 可能恰好跨在同一 Cache Line 的边界两侧——两线程交替写入时，性能可能暴跌 **10~100 倍**。

> [!note] 为什么 ECS 特别容易被忽视？
> 开发者往往会认为"SoA 已经做了数据导向设计，缓存肯定没问题"，却忽略了**多线程切分粒度**的问题。现代 ECS（如 Unity DOTS）的典型做法是：一个 Chunk（如 128 个 Entity）只被一个线程处理，不同线程处理不同 Chunk，从根本上避免同 Chunk 内的 False Sharing。

**更高级的策略**：除了 Padding，还可以使用 **per-core 本地缓冲 + 批量合并写**（类似 Linux kernel 的 per-cpu 变量、Disruptor 的批处理模式），从根本上减少跨核同步频率。

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
> - [[Notes/操作系统/性能分析与工具/Cache-Miss分析实战]] — 用 perf 量化 Cache Miss

---
title: SoA、AoS 与 AOSOA
date: 2026-06-13
tags:
  - C++
  - SoA
  - AoS
  - AOSOA
  - 数据布局
  - 缓存友好
  - ECS
aliases:
  - SoA
  - AoS
  - AOSOA
  - Data-Oriented Design
---

> [[Notes/C++编程/索引|← 返回 C++ 编程索引]]

# SoA、AoS 与 AOSOA

> [!info] 一句话概括
> **AoS** 把多个字段打包成一个对象，符合直觉但缓存利用率低；**SoA** 把同类型字段分开连续存放，牺牲直观性换取 SIMD 友好和高吞吐；**AOSOA** 则在两者之间做折中，兼顾代码可读性和缓存效率。

---

## 问题 0：为什么数据布局会影响性能？

假设你有一个粒子系统，每个粒子有位置、速度和生命期：

```cpp
struct Particle {
    Vec3 position;
    Vec3 velocity;
    float lifetime;
};

Particle particles[10000];
```

现在你想更新所有粒子的位置：

```cpp
for (auto& p : particles) {
    p.position += p.velocity * dt;
}
```

这段代码只访问 `position` 和 `velocity`，但 CPU 从内存加载一个 `Particle` 时，会把整个结构体（包括不用的 `lifetime`）一起加载进缓存。如果 `lifetime` 很大，那么每次加载的有效数据比例就很低——这叫**低缓存利用率**。

更严重的是，如果 `position`、`velocity`、`lifetime` 大小不同，结构体内部还会有**填充字节**，进一步浪费带宽。

> [!abstract]
> **数据布局（Data Layout）**决定 CPU 访问数据时的缓存命中率和 SIMD 并行度。同样的算法，不同的布局，性能可能相差数倍。

---

## 问题 1：AoS 是什么？优缺点？

**AoS = Array of Structs**，也就是我们最自然的写法：一个数组，每个元素是一个结构体。

```cpp
// AoS
struct Particle {
    Vec3 position;
    Vec3 velocity;
    float lifetime;
};

std::vector<Particle> particles;
```

### 优点

- **直观**：一个粒子就是一个对象，代码好写。
- **局部性好（单对象视角）**：如果某段代码同时访问一个对象的所有字段，AoS 很棒。

### 缺点

- **缓存利用率低（多对象单字段视角）**：如果算法只访问部分字段，其他字段会挤占缓存。
- **SIMD 不友好**：想把 4 个粒子的 `position.x` 一起加载到 SIMD 寄存器，需要跨结构体跳跃读取。

---

## 问题 2：SoA 是什么？为什么 ECS 用它？

**SoA = Structure of Arrays**，把每个字段单独存一个数组：

```cpp
// SoA
struct ParticleSoA {
    std::vector<Vec3> positions;
    std::vector<Vec3> velocities;
    std::vector<float> lifetimes;
};
```

现在更新位置：

```cpp
for (size_t i = 0; i < n; ++i) {
    particles.positions[i] += particles.velocities[i] * dt;
}
```

### 优点

- **缓存友好**：只加载 `positions` 和 `velocities`，`lifetimes` 不会被无辜加载。
- **SIMD 友好**：`positions[i].x`、`positions[i+1].x`... 连续存放，可以直接用 SIMD 指令批量处理。
- **剔除和过滤高效**：如果某个粒子死亡了，只需要把相关数组的条目 remove，不需要搬动整个对象。

### 缺点

- **不直观**：一个粒子的所有字段分散在多个数组里。
- **单对象访问麻烦**：想打印一个粒子的全部信息，要访问多个数组。
- **关联数组需要同步**：增删改时要保证所有数组的索引一致。

> [!tip]
> ECS（Entity-Component-System）架构的核心就是把 Component 按 SoA 存储。每个 System 只遍历自己关心的 Component 数组，既缓存友好又便于并行。

---

## 问题 3：AOSOA 是什么？什么时候用它？

AOSOA = Array of Structure of Arrays，是 AoS 和 SoA 的折中。

基本思想：把数据分成若干块，每块内部用 SoA，块与块之间是 AoS。

```cpp
// AOSOA：每 4 个粒子为一组，组内 SoA
struct ParticleBlock {
    Vec3 positions[4];
    Vec3 velocities[4];
    float lifetimes[4];
};

std::vector<ParticleBlock> blocks;
```

### 优点

- **兼顾直观与性能**：单个块内还是对象感，但块内字段连续存放。
- **SIMD 友好**：块内同一字段连续，可以用 SIMD。
- **减少指针间接**：相比完全 SoA，AOSOA 的块内字段靠偏移访问，不需要多个独立数组指针。

### 缺点

- **实现复杂**：需要处理块大小对齐、未满块等问题。
- **灵活性不如纯 SoA**：块大小固定，不适合频繁变长的数据。

---

## 问题 4：Day 68 的 ComponentArray 为什么用 SoA？

ECS 架构中，Component 通常只包含单一类型的数据（如 `Transform`、`Health`、`Velocity`）。System（如 `MovementSystem`）只关心特定 Component：

```cpp
// 所有 Transform 连续存放
std::vector<Transform> transforms;

// MovementSystem 只遍历 transforms，不需要关心其他 Component
for (auto& t : transforms) {
    t.position += t.velocity * dt;
}
```

如果所有 Component 都按 AoS 存在一个「GameObject」里，那么遍历 `Transform` 时会把不相干的 `Mesh`、`Audio`、`AI` 数据也加载进缓存，严重浪费带宽。

SoA 让同一 System 访问的数据高度紧凑，是 ECS 高性能的关键。

---

## 总结

| 布局 | 结构 | 优点 | 缺点 | 适用场景 |
|------|------|------|------|---------|
| **AoS** | 数组的每个元素是一个完整对象 | 直观、单对象局部性好 | 缓存利用率低、SIMD 不友好 | 单对象多字段频繁访问 |
| **SoA** | 每个字段单独一个数组 | 缓存友好、SIMD 友好、过滤高效 | 不直观、索引同步麻烦 | ECS、粒子系统、大规模同构数据 |
| **AOSOA** | 分块，块内 SoA | 兼顾直观与性能 | 实现复杂 | 固定大小批次、SIMD 向量化 |

- 数据布局是「面向数据设计」的核心，直接影响缓存命中和 SIMD 效率。
- ECS 架构用 SoA 存储 Component，让 System 只访问自己需要的数据。
- 手写 ComponentArray 时，关键是保证同类型 Component 在内存中连续存放，并处理好索引到 Entity 的映射。

---

## 引擎映射

| 引擎 | 应用场景 | 具体体现 |
|------|---------|---------|
| **SelfGameEngine** | ECS ComponentArray | `ComponentArray<T>` 用单一连续数组存储某一类型的所有 Component，`System` 顺序遍历，配合 SIMD 优化 |
| **UE** | Niagara 粒子、Mass 框架 | UE 的 Mass 框架采用 SoA 存储 Entity Component；Niagara 粒子系统用 SoA/AOSOA 优化粒子模拟 |

> [!note] 关键取舍
> AoS 适合「对象导向」的思维方式和编辑器工具链；SoA 适合「数据导向」的高性能批量计算。现代引擎往往在编辑器/脚本层用 AoS 表达，在运行时 Core 层用 SoA/AOSOA 存储。ECS 架构就是这一取舍的工业级实现。

---

> 相关笔记：
> - [[Notes/C++编程/对象内存模型与底层机制/对象内存布局：从 struct 到 class|对象内存布局]] — 结构体内存排布与填充
> - [[Notes/C++编程/对象内存模型与底层机制/缓存行、false sharing 与内存布局|缓存行、false sharing 与内存布局]]（尚未产出）— 缓存行与伪共享
> - [[Notes/C++编程/对象内存模型与底层机制/内存对齐规则与 SIMD 对齐|内存对齐规则与 SIMD 对齐]]（尚未产出）— SIMD 对齐要求

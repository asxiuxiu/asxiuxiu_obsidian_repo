---
title: Cache-Miss分析实战
date: 2026-04-18
tags:
  - 操作系统
  - 性能分析
  - Cache
  - 优化实战
aliases:
  - Cache Miss Analysis
---

> [← 返回 性能分析与工具索引]([[索引\|性能分析与工具索引]])

# Cache-Miss 分析实战

---

## Why：为什么 Cache Miss 是头号性能杀手？

- CPU 速度远快于内存，Cache Miss 意味着等待数百个周期。
- 游戏引擎中常见的 Cache Miss 场景：
  - ECS 遍历不连续内存（指针跳跃）。
  - 粒子系统用链表管理。
  - 多线程 False Sharing。
  - 矩阵/图像处理不按块分治。
- **本阶段目标**：用工具定位 Miss，用代码验证优化效果。

---

## What：核心概念

### LLC-load-misses

- **LLC（Last Level Cache）Miss**：数据不在 L1/L2/L3，需从主存加载。
- 这是最需要关注的指标，因为 L1 Miss 可能被 L2/L3 掩盖，但 LLC Miss 一定 stall。

### perf c2c（Cache-to-Cache）

```bash
# Linux: 检测 False Sharing
perf c2c record ./program
perf c2c report
```

- 能显示哪些 Cache Line 被多个核心竞争修改，直接定位 False Sharing。

### Intel VTune Memory Access

- 可视化显示每个内存访问的延迟、是否命中缓存、是否跨 NUMA 节点。

---

## How：优化方法论

1. **测量基线**：`perf stat -e LLC-load-misses`。
2. **缩小范围**：`perf record -e cache-misses` + `perf report` 找到热点函数。
3. **针对性优化**：
   - 连续内存 → 连续访问。
   - 大数据集 → 分块（Tiling/Blocking）使其适应缓存。
   - 共享变量 → 按 Cache Line 对齐隔离。
4. **验证**：再次测量，确认 Miss 下降、耗时缩短。

---

## C++ 本地验证实验

### 实验 1：矩阵乘法 Cache Blocking

```cpp
// workspace/os-lab/profiling/matrix_multiply.cpp
#include <vector>
#include <chrono>
#include <iostream>

constexpr int N = 1024;

void naive(const float* A, const float* B, float* C) {
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j) {
            float sum = 0;
            for (int k = 0; k < N; ++k)
                sum += A[i * N + k] * B[k * N + j];
            C[i * N + j] = sum;
        }
}

void blocked(const float* A, const float* B, float* C, int block = 64) {
    for (int ii = 0; ii < N; ii += block)
        for (int jj = 0; jj < N; jj += block)
            for (int kk = 0; kk < N; kk += block)
                for (int i = ii; i < std::min(ii + block, N); ++i)
                    for (int j = jj; j < std::min(jj + block, N); ++j) {
                        float sum = C[i * N + j];
                        for (int k = kk; k < std::min(kk + block, N); ++k)
                            sum += A[i * N + k] * B[k * N + j];
                        C[i * N + j] = sum;
                    }
}

int main() {
    std::vector<float> A(N * N, 1.0f), B(N * N, 1.0f), C(N * N, 0.0f);

    auto t1 = std::chrono::high_resolution_clock::now();
    naive(A.data(), B.data(), C.data());
    auto t2 = std::chrono::high_resolution_clock::now();

    std::fill(C.begin(), C.end(), 0.0f);
    auto t3 = std::chrono::high_resolution_clock::now();
    blocked(A.data(), B.data(), C.data());
    auto t4 = std::chrono::high_resolution_clock::now();

    std::cout << "Naive : "
              << std::chrono::duration<double, std::milli>(t2 - t1).count()
              << " ms\n";
    std::cout << "Blocked: "
              << std::chrono::duration<double, std::milli>(t4 - t3).count()
              << " ms\n";
    return 0;
}
```

**验证命令**：
```bash
g++ -O2 matrix_multiply.cpp -o matrix_multiply
perf stat -e LLC-load-misses,cycles ./matrix_multiply
```

**预期结果**：Blocked 版本通常快 **3-10 倍**，LLC-load-misses 大幅下降。

### 实验 2：False Sharing 定位

```cpp
// 使用 [[CPU与内存架构/内存对齐与填充]] 中的 false_sharing.cpp
// perf c2c record ./false_sharing
// perf c2c report
```

---

## 游戏引擎延伸

### 粒子系统优化

- **问题**：每帧更新大量粒子，若用 `std::list<Particle>`，每个粒子随机分配，Cache Miss 极高。
- **优化**：
  - 改用 `std::vector<Particle>`，只标记死亡粒子，批量压缩。
  - SoA 布局：`Positions[]`, `Velocities[]`, `Lifetimes[]` 分开存储，只更新存活字段。
  - 分块更新：每 1024 个粒子为一个块，适应 L1 缓存。

### 渲染命令排序

- **问题**：提交 Draw Call 时，材质/Shader/纹理频繁切换，导致 GPU 状态变化和 CPU 端缓存抖动。
- **优化**：按 Shader -> 材质 ->  mesh 排序，保证遍历时的状态连续性，减少 CPU 端材质对象的 Cache Miss。

---

> [!info] 延伸阅读
> - [[CPU与内存架构/CPU缓存体系]] — Cache 优化的理论基础
> - [[CPU与内存架构/内存对齐与填充]] — False Sharing 的原理与解法

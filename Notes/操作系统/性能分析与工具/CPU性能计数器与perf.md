---
title: CPU性能计数器与perf
date: 2026-04-18
tags:
  - 操作系统
  - 性能分析
  - perf
  - PMU
aliases:
  - CPU Performance Counters
---

> [← 返回 性能分析与工具索引]([[索引\|性能分析与工具索引]])

# CPU 性能计数器与 perf

---

## Why：为什么要用硬件性能计数器？

- **代码级优化盲人摸象**：只知道"慢"，不知道"为什么慢"（是 Cache Miss？分支预测失败？还是指令依赖？）。
- **PMU（Performance Monitoring Unit）**：现代 CPU 内置的硬件计数器，可精确统计：
  - 指令数（Instructions）
  - 周期数（Cycles）
  - Cache Misses（L1/L2/LLC）
  - 分支预测失败（Branch Misses）
  - TLB Misses、Stalled Cycles 等
- **数据驱动优化**：找到真正的瓶颈，避免盲目优化。

---

## What：核心概念

### CPI / IPC

- **CPI（Cycles Per Instruction）**：每条指令消耗的平均周期。CPI 高说明流水线 stalls 多。
- **IPC（Instructions Per Cycle）**：每周期执行的指令数。IPC 高说明 CPU 利用率高。
- **目标**：优化热点循环，降低 CPI / 提升 IPC。

### 常用 perf 命令

```bash
# 统计程序整体性能事件
perf stat -e cycles,instructions,cache-references,cache-misses ./program

# 记录采样，生成火焰图
perf record -g ./program
perf report

# 实时查看热点函数
perf top
```

### Windows 替代工具

- **Intel VTune**：最全面的性能分析工具，支持 Microarchitecture Analysis。
- **Windows Performance Analyzer (WPA)**：基于 ETW，分析系统级事件。
- **Visual Studio Profiler**：集成度高，适合快速分析。

---

## How：如何分析？

1. **先看宏观指标**：`instructions / cycles` 比值，判断是计算 bound 还是内存 bound。
2. **Cache Miss 高** → 优化数据布局和访问模式。
3. **Branch Miss 高** → 减少分支、排序数据、用条件移动代替分支。
4. **Stalled Cycles 高** → 检查依赖链、内存延迟、除法/开方等慢指令。

---

## C++ 本地验证实验

### 实验：用 perf 对比数组与链表

```bash
# Linux
# 编译: g++ -O2 array_vs_list.cpp -o array_vs_list
# 运行: perf stat -e cycles,instructions,cache-misses,L1-dcache-load-misses ./array_vs_list
```

**预期结果**：
- 链表版本的 `cache-misses` 和 `L1-dcache-load-misses` 远高于数组。
- 链表版本的 `cycles` 更高，`instructions` 也可能更高（指针解引用）。

### 实验：计算 CPI

```bash
perf stat -e cycles,instructions ./your_program
# CPI = cycles / instructions
# IPC = instructions / cycles
```

---

## 游戏引擎延伸

### 引擎模块的瓶颈识别

- **渲染线程 IPC 低**：可能是 Draw Call 太多（CPU bound），或常量缓冲区更新导致 Cache Miss（内存 bound）。
- **物理模拟 CPI 高**：可能是分支预测失败（碰撞检测的 if/else 树），或 SIMD 指令未对齐。
- **加载线程 Cache Miss 高**：资源解压或序列化时随机访问数据结构，改为顺序流式读取。

### 集成到 CI

- 在自动化测试中加入 `perf stat` 基准，监控关键场景的 cycles 变化。
- 若某次提交导致 `cache-misses` 上升 50%，自动告警。

---

> [!info] 延伸阅读
> - [[Cache-Miss分析实战]] — 专门针对 Cache 的深入分析
> - [[Tracy与游戏引擎Profiler]] — 可视化时间线，与硬件计数器互补

---
title: 系统调用与vDSO
date: 2026-04-18
tags:
  - 操作系统
  - 系统调用
  - vDSO
  - 性能优化
aliases:
  - Syscall and vDSO
---

> [← 返回 系统调用与IO索引]([[索引\|系统调用与IO索引]])

# 系统调用与 vDSO

---

## Why：为什么要减少系统调用？

- **用户态 / 内核态切换**：触发中断或 `syscall` 指令，保存寄存器、切换栈、刷新 TLB/缓存，**单次开销约 100ns-1μs**。
- **游戏引擎的敏感路径**：每帧一次的主循环中，若频繁 `malloc`（可能 `brk`/`mmap`）、获取时间戳、查询文件状态，累积的 syscall 开销会导致微卡顿。
- **vDSO（virtual Dynamic Shared Object）**：Linux 将部分无状态内核数据（如时间、CPU 周期）映射到用户态，**完全避免 syscall**。

---

## What：核心概念

### 系统调用流程

```
用户代码 -> syscall 指令 -> CPU 特权级切换 -> 内核入口 -> 执行 -> 返回用户态
```

- **Spectre/Meltdown 补丁后**：部分系统调用还需刷新 CPU 缓冲区，开销进一步增加。

### 常见触发 syscall 的操作

| 操作 | 原因 |
|------|------|
| `new` / `malloc` | 可能触发 `brk` / `mmap` |
| `std::chrono` 时间获取 | Linux 旧版通过 `clock_gettime` syscall |
| 文件读写 | `read` / `write` / `open` |
| 线程同步 | `futex`（`std::mutex` 底层） |
| 网络收发 | `send` / `recv` |

### vDSO

- Linux 内核将一小段代码和数据页映射到每个进程的用户空间。
- `gettimeofday`, `clock_gettime`, `getcpu` 等可通过 vDSO **在用户态直接执行**。
- 现代 glibc 的 `clock_gettime(CLOCK_MONOTONIC)` 默认走 vDSO，无需 syscall。

---

## How：如何减少 syscall？

1. **批量处理**：一次性读取大缓冲，而非逐字节。
2. **用户态缓冲**：自己维护缓冲区，减少 `read`/`write` 次数。
3. **预分配内存**：游戏启动时预分配足够内存，运行时避免 `new`/`malloc`。
4. **使用 `rdtsc` / `QueryPerformanceCounter`**：Windows 高分辨率计时器走用户态；Linux `CLOCK_MONOTONIC` 走 vDSO。

---

## C++ 本地验证实验

### 实验 1：统计程序的 syscall 次数

```bash
# Linux: 使用 strace
cd workspace/os-lab/syscall
strace -c ./your_program

# Windows: 使用 ETW / WPA 或 Process Monitor
```

### 实验 2：高频时间获取对比

```cpp
// workspace/os-lab/syscall/time_bench.cpp
#include <chrono>
#include <iostream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

constexpr int N = 10'000'000;

int main() {
    // 1. std::chrono (通常走 vDSO / QPC)
    auto t1 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < N; ++i) {
        volatile auto now = std::chrono::steady_clock::now();
        (void)now;
    }
    auto t2 = std::chrono::high_resolution_clock::now();

#ifdef _WIN32
    // 2. QueryPerformanceCounter
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    auto t3 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < N; ++i) {
        QueryPerformanceCounter(&cnt);
    }
    auto t4 = std::chrono::high_resolution_clock::now();
#endif

    std::cout << "chrono: "
              << std::chrono::duration<double, std::milli>(t2 - t1).count()
              << " ms\n";
#ifdef _WIN32
    std::cout << "QPC   : "
              << std::chrono::duration<double, std::milli>(t4 - t3).count()
              << " ms\n";
#endif
    return 0;
}
```

**预期结果**：`std::chrono` 与平台 API 耗时接近（均在几十纳秒级），说明未走 syscall。

---

## 游戏引擎延伸

### 游戏循环的时间戳获取

- 每帧开头获取时间戳用于计算 `DeltaTime`。
- 若错误地使用 `gettimeofday`（可能受 NTP 影响）或旧的 syscall 方式，既慢又不精确。
- **最佳实践**：
  - Windows：`QueryPerformanceCounter`（经 `std::chrono::steady_clock` 封装）。
  - Linux：`clock_gettime(CLOCK_MONOTONIC)`（经 vDSO）。

### 内存分配器与 syscall

- 通用分配器在内存不足时会 `mmap`/`VirtualAlloc`，触发 syscall 并可能阻塞。
- 引擎自定义分配器（如 Linear Allocator）在初始化时一次性 `mmap` 大块内存，之后全程用户态管理。

---

> [!info] 延伸阅读
> - [[文件IO与mmap]] — 减少文件 IO 中的数据拷贝和 syscall
> - [[高性能网络IO模型]] — 网络场景下的 syscall 优化

---
title: 自定义内存池与Arena分配器
date: 2026-04-18
tags:
  - 操作系统
  - 内存管理
  - 分配器
  - Arena
  - Pool
aliases:
  - Custom Memory Pool
---

> [[索引|← 返回 内存管理索引]]

# 自定义内存池与 Arena 分配器

---

## Why：为什么要自己写分配器？

- **确定性**：通用分配器的行为不可预测，自定义分配器可保证 O(1) 分配与释放，无系统调用。
- **无碎片**：针对特定使用模式（如大量同尺寸对象、一帧内批量分配），可完全消除碎片。
- **Cache Friendly**：控制内存布局，按使用顺序连续分配，提升缓存命中率。
- **调试友好**：可嵌入 guard band、分配跟踪、泄漏检测。

---

## What：常见分配器类型

### 1. Linear Allocator（线性/栈式分配器）

- 维护一个指针 `offset`，分配时只需原子加/普通加，释放时只能**整体回滚**或**按栈顺序回滚**。
- 适用场景：一帧内的临时内存（Temp Allocator），帧结束时全部清空。

### 2. Pool Allocator（对象池）

- 预分配 N 个固定大小的块，用自由链表（Free List）管理空闲块。
- 分配：从 free list 弹出一个节点；释放：压回 free list。
- 适用场景：粒子、事件、网络包等大量同尺寸短生命周期对象。

### 3. Stack Allocator

- 类似 Linear，但支持标记（Marker）和回滚到标记。
- 适用场景：有明确作用域的内存需求，如关卡加载。

### 4. Free List Allocator

- 管理不同大小的块，支持任意分配和释放，类似简化版 `malloc`。
- 实现复杂，通常只在需要替换系统分配器时使用。

---

## How：如何实现？

### Linear Allocator 最小实现

```cpp
class LinearAllocator {
    char* buffer;
    size_t capacity;
    size_t offset;
public:
    LinearAllocator(size_t cap) : capacity(cap), offset(0) {
        buffer = (char*)std::aligned_alloc(64, cap); // 64B 对齐
    }
    void* alloc(size_t size, size_t align = 8) {
        size_t aligned = (offset + align - 1) & ~(align - 1);
        if (aligned + size > capacity) return nullptr;
        void* p = buffer + aligned;
        offset = aligned + size;
        return p;
    }
    void reset() { offset = 0; }
    ~LinearAllocator() { std::free(buffer); }
};
```

### Pool Allocator 最小实现

```cpp
class PoolAllocator {
    struct Node { Node* next; };
    Node* free_list = nullptr;
    char* buffer;
    size_t block_size;
    size_t block_count;
public:
    PoolAllocator(size_t obj_size, size_t count, size_t align = 8)
        : block_size(std::max(obj_size, sizeof(Node))), block_count(count) {
        buffer = (char*)std::aligned_alloc(align, block_size * count);
        for (size_t i = 0; i < count; ++i) {
            Node* node = (Node*)(buffer + i * block_size);
            node->next = free_list;
            free_list = node;
        }
    }
    void* alloc() {
        if (!free_list) return nullptr;
        Node* node = free_list;
        free_list = free_list->next;
        return node;
    }
    void free(void* p) {
        Node* node = (Node*)p;
        node->next = free_list;
        free_list = node;
    }
    ~PoolAllocator() { std::free(buffer); }
};
```

---

## C++ 本地验证实验

### 实验：Pool Allocator vs malloc 性能

```cpp
// workspace/os-lab/memory/pool_bench.cpp
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <cstdlib>

// 将上面的 PoolAllocator 定义放在这里...

struct Particle { float x, y, z, vx, vy, vz; };

constexpr int N = 100'000;
constexpr int ITER = 1000;

void bench_pool() {
    PoolAllocator pool(sizeof(Particle), N, alignof(Particle));
    auto t1 = std::chrono::high_resolution_clock::now();
    for (int it = 0; it < ITER; ++it) {
        std::vector<void*> ptrs;
        ptrs.reserve(N);
        for (int i = 0; i < N; ++i) ptrs.push_back(pool.alloc());
        for (auto p : ptrs) pool.free(p);
    }
    auto t2 = std::chrono::high_resolution_clock::now();
    std::cout << "Pool: "
              << std::chrono::duration<double, std::milli>(t2 - t1).count()
              << " ms\n";
}

void bench_malloc() {
    auto t1 = std::chrono::high_resolution_clock::now();
    for (int it = 0; it < ITER; ++it) {
        std::vector<void*> ptrs;
        ptrs.reserve(N);
        for (int i = 0; i < N; ++i) ptrs.push_back(std::malloc(sizeof(Particle)));
        for (auto p : ptrs) std::free(p);
    }
    auto t2 = std::chrono::high_resolution_clock::now();
    std::cout << "Malloc: "
              << std::chrono::duration<double, std::milli>(t2 - t1).count()
              << " ms\n";
}

int main() {
    bench_pool();
    bench_malloc();
    return 0;
}
```

**预期结果**：Pool Allocator 通常快 **5-50 倍**，且不产生碎片。

---

## 游戏引擎延伸

### UE 的 Memory Framework

- `FMemory::Malloc` 可全局替换，但模块级更常用 **定制分配器**。
- `TMemStack`：线性分配器，用于渲染命令、临时字符串等。
- `TBlockAllocator` / `TSparseArray`：基于 Pool 思想的索引管理，广泛用于 UObject 和组件系统。

### 自研引擎的最小实践

- **每帧一个 Temp Arena**：渲染提取（Render Extract）阶段所有临时分配走 Arena，Present 后 reset。
- **按子系统划分 Pool**：物理、音频、网络各自管理 Pool，避免跨系统碎片。
- **禁止运行时 Heap 分配**：在关键路径（如游戏逻辑 Tick、渲染提交）中，所有内存必须预分配或走 Pool。

---

> [!info] 延伸阅读
> - [[虚拟内存与地址翻译]] — 理解分配器向 OS 申请内存的底层
> - [[Notes/操作系统/CPU与内存架构/内存对齐与填充]] — 保证 Pool 内对象对齐

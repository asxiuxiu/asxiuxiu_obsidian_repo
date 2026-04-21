---
title: 文件IO与mmap
date: 2026-04-18
tags:
  - 操作系统
  - 文件IO
  - mmap
  - 资源加载
aliases:
  - File IO and mmap
---

> [← 返回 系统调用与IO索引]([[索引\|系统调用与IO索引]])

# 文件 IO 与 mmap

---

## Why：为什么文件 IO 是游戏卡顿主因？

- **资源密集型**：现代游戏资源（纹理、模型、音频）动辄数 GB。
- **同步阻塞**：传统 `read` 需从磁盘 -> 内核页缓存 -> 用户缓冲区，两次数据拷贝 + 可能的阻塞等待。
- **mmap 的优势**：将文件直接映射到进程地址空间，**零拷贝**（Zero Copy），按需加载（Demand Paging）。

---

## What：核心概念

### 传统 read/write

```
磁盘 -> 内核 Page Cache -> 用户缓冲区 -> 使用
```

- 两次数据拷贝，一次用户态/内核态切换。
- 适合小文件、需要修改内容的场景。

### mmap（Memory Map）

```
磁盘 -> 内核 Page Cache <- 直接映射到用户地址空间
```

- 用户代码直接读写内存，OS 负责缺页时从磁盘加载。
- 适合大文件只读/少量写、随机访问、共享内存。

### Page Cache 与预读

- OS 会自动预读顺序访问的文件页（Read-ahead）。
- `madvise(MADV_SEQUENTIAL)` / `FILE_FLAG_SEQUENTIAL_SCAN` 提示 OS 优化预读策略。
- `mlock` / `VirtualLock` 可将页锁定在物理内存，防止被换出。

---

## How：如何选择？

| 场景 | 推荐方案 | 原因 |
|------|----------|------|
| 小配置文件 | `read` 一次性读入 | 简单，无映射开销 |
| 大资源包（只读） | `mmap` | 零拷贝，按需加载，多进程共享 |
| 日志写入 | 用户态缓冲 + `write` 批量刷盘 | 减少 syscall |
| 热更新资源 | `mmap` + `mlock` | 保证常驻内存 |

---

## C++ 本地验证实验

### 实验：read vs mmap 大文件遍历

```cpp
// workspace/os-lab/syscall/read_vs_mmap.cpp
// Linux: g++ read_vs_mmap.cpp -o read_vs_mmap
// 生成测试文件: dd if=/dev/zero of=testfile bs=1M count=512

#include <iostream>
#include <chrono>
#include <vector>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

constexpr size_t BLOCK = 4096;

void bench_read(const char* path) {
#ifdef _WIN32
    HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    LARGE_INTEGER size; GetFileSizeEx(hFile, &size);
    std::vector<char> buf(BLOCK);
    DWORD read;
    auto t1 = std::chrono::high_resolution_clock::now();
    for (LONG64 off = 0; off < size.QuadPart; off += BLOCK) {
        ReadFile(hFile, buf.data(), BLOCK, &read, NULL);
        volatile char sum = 0;
        for (size_t i = 0; i < read; ++i) sum += buf[i];
    }
    auto t2 = std::chrono::high_resolution_clock::now();
    CloseHandle(hFile);
#else
    int fd = open(path, O_RDONLY);
    struct stat st; fstat(fd, &st);
    std::vector<char> buf(BLOCK);
    auto t1 = std::chrono::high_resolution_clock::now();
    for (off_t off = 0; off < st.st_size; off += BLOCK) {
        ssize_t n = read(fd, buf.data(), BLOCK);
        volatile char sum = 0;
        for (ssize_t i = 0; i < n; ++i) sum += buf[i];
    }
    auto t2 = std::chrono::high_resolution_clock::now();
    close(fd);
#endif
    std::cout << "read: "
              << std::chrono::duration<double, std::milli>(t2 - t1).count()
              << " ms\n";
}

void bench_mmap(const char* path) {
#ifdef _WIN32
    HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    HANDLE hMap = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    char* data = (char*)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    LARGE_INTEGER size; GetFileSizeEx(hFile, &size);
    auto t1 = std::chrono::high_resolution_clock::now();
    for (LONG64 off = 0; off < size.QuadPart; off += BLOCK) {
        volatile char sum = 0;
        for (size_t i = 0; i < BLOCK && off + i < (size_t)size.QuadPart; ++i)
            sum += data[off + i];
    }
    auto t2 = std::chrono::high_resolution_clock::now();
    UnmapViewOfFile(data); CloseHandle(hMap); CloseHandle(hFile);
#else
    int fd = open(path, O_RDONLY);
    struct stat st; fstat(fd, &st);
    char* data = (char*)mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    auto t1 = std::chrono::high_resolution_clock::now();
    for (off_t off = 0; off < st.st_size; off += BLOCK) {
        volatile char sum = 0;
        for (size_t i = 0; i < BLOCK && off + i < st.st_size; ++i)
            sum += data[off + i];
    }
    auto t2 = std::chrono::high_resolution_clock::now();
    munmap(data, st.st_size); close(fd);
#endif
    std::cout << "mmap: "
              << std::chrono::duration<double, std::milli>(t2 - t1).count()
              << " ms\n";
}

int main(int argc, char* argv[]) {
    const char* file = (argc > 1) ? argv[1] : "testfile";
    bench_read(file);
    bench_mmap(file);
    return 0;
}
```

**预期结果**：mmap 通常略快或持平，但在多进程共享、随机访问、大文件场景下优势显著。

---

## 游戏引擎延伸

### 虚拟文件系统与 Pak 文件

- UE 的 `FPakFile` 将资源打包为 `.pak`，支持 `mmap` 映射整个 Pak。
- 读取资源时，只需返回文件在 mmap 区域中的指针，无需拷贝。
- 多进程游戏客户端（如登录器 + 游戏）可共享同一份 Pak 的 Page Cache。

### 运行时资源加载策略

- **预加载（Preload）**：关卡切换前，用独立线程 `mmap` + `mlock` 锁定下一关资源。
- **流式加载（Streaming）**：大地形/纹理分块存储，相机靠近时通过 mmap 的 Page Fault 自然触发加载。
- **异步 IO**：配合 IOCP / io_uring，在 IO 线程中完成文件读取，不阻塞游戏线程。

---

> [!info] 延伸阅读
> - [[Notes/操作系统/内存管理/虚拟内存与地址翻译]] — mmap 的页表机制
> - [[高性能网络IO模型]] — 异步 IO 与事件驱动

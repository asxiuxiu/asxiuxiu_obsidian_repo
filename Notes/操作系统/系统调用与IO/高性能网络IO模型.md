---
title: 高性能网络IO模型
date: 2026-04-18
tags:
  - 操作系统
  - 网络IO
  - 并发
  - 游戏服务器
aliases:
  - High Performance Network IO
---

> [← 返回 系统调用与IO索引]([[索引\|系统调用与IO索引]])

# 高性能网络 IO 模型

---

## Why：为什么游戏服务器需要高性能网络 IO？

- **MMO / 大世界游戏**：单服需支撑数万到数十万并发连接。
- **实时性要求**：状态同步、帧同步对延迟敏感，网络层不能成为瓶颈。
- **C10K / C10M 问题**：传统阻塞模型无法支撑海量连接，需要事件驱动和非阻塞 IO。

---

## What：核心模型

### 阻塞 IO（Blocking IO）

- 一连接一线程。线程在 `recv` 时阻塞等待数据。
- 简单，但线程数随连接数线性增长，上下文切换开销巨大。

### 非阻塞 IO + 多路复用

- **select / poll**：遍历所有 fd 检查状态，fd 数量大时效率低（O(n)）。
- **epoll（Linux）**：内核维护就绪队列，用户只拿到活跃的 fd（O(1)）。支持 ET（边缘触发）和 LT（水平触发）。
- **IOCP（Windows）**：完成端口，真正的异步 IO。提交 IO 请求后继续执行，完成后内核回调/通知。
- **io_uring（Linux 5.1+）**：共享环形缓冲区，用户态提交 SQE，内核完成回填 CQE，减少 syscall。

### Reactor vs Proactor

| 模式 | 代表 | 特点 |
|------|------|------|
| **Reactor** | epoll, select | 事件通知「可读/可写」，用户代码执行 IO |
| **Proactor** | IOCP, io_uring | 事件通知「已完成」，内核执行 IO |

---

## How：如何选型？

- **跨平台游戏服务器**：Windows 用 IOCP，Linux 用 epoll，封装统一接口。
- **追求极致性能**：Linux 下尝试 io_uring，syscall 次数最少。
- **连接数 < 1000**：阻塞 IO 或 `std::thread` 每连接可能更简单。

---

## C++ 本地验证实验

### 实验：Windows IOCP Echo Server（最小实现）

```cpp
// workspace/os-lab/syscall/iocp_echo.cpp
// Windows only: cl iocp_echo.cpp /EHsc ws2_32.lib
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <iostream>

#pragma comment(lib, "ws2_32.lib")

struct PerIoData {
    OVERLAPPED overlapped;
    WSABUF wsaBuf;
    char buffer[1024];
    DWORD opcode; // 0=accept, 1=recv, 2=send
};

int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(7777);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(listenSock, (sockaddr*)&addr, sizeof(addr));
    listen(listenSock, SOMAXCONN);

    HANDLE iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    CreateIoCompletionPort((HANDLE)listenSock, iocp, 0, 0);

    std::cout << "IOCP Echo Server on port 7777\n";

    // 投递初始接收
    PerIoData* io = new PerIoData{};
    io->opcode = 1;
    io->wsaBuf.buf = io->buffer;
    io->wsaBuf.len = sizeof(io->buffer);
    DWORD flags = 0;
    WSARecv(listenSock, &io->wsaBuf, 1, NULL, &flags, &io->overlapped, NULL);

    DWORD bytes;
    ULONG_PTR key;
    OVERLAPPED* ol;
    while (GetQueuedCompletionStatus(iocp, &bytes, &key, &ol, INFINITE)) {
        PerIoData* io = CONTAINING_RECORD(ol, PerIoData, overlapped);
        if (io->opcode == 1) { // recv complete
            io->opcode = 2;
            io->wsaBuf.len = bytes;
            WSASend(listenSock, &io->wsaBuf, 1, NULL, 0, &io->overlapped, NULL);
        } else if (io->opcode == 2) { // send complete
            io->opcode = 1;
            io->wsaBuf.len = sizeof(io->buffer);
            DWORD flags = 0;
            WSARecv(listenSock, &io->wsaBuf, 1, NULL, &flags, &io->overlapped, NULL);
        }
    }
    return 0;
}
```

### 实验 2：Linux epoll Echo Server（思路）

```cpp
// workspace/os-lab/syscall/epoll_echo.cpp (Linux)
// 核心流程：
// 1. socket -> fcntl(O_NONBLOCK)
// 2. epoll_create1 -> epoll_ctl(EPOLL_CTL_ADD, EPOLLIN)
// 3. epoll_wait -> 遍历 events -> recv/send
```

---

## 游戏引擎延伸

### 游戏服务器网络层

- **状态同步**（如 FPS、MOBA）：服务器以固定频率（如 30Hz）广播世界状态。
  - 网络层需保证高吞吐和低延迟，IOCP/epoll 处理万级连接。
  - 使用 UDP + 可靠性层（如 ENET、KCP），避免 TCP 拥塞控制导致的延迟波动。
- **帧同步**（如 RTS、格斗游戏）：只同步输入，客户端确定性模拟。
  - 对延迟更敏感，通常用 UDP；网络层需处理丢包和乱序。

### 引擎客户端网络

- 客户端通常单连接，阻塞或非阻塞均可。
- 资源下载（如补丁、DLC）可走 HTTP/2 或自定义协议，利用 `mmap` 写入磁盘。

---

> [!info] 延伸阅读
> - [[系统调用与vDSO]] — 理解网络 IO 中的 syscall 开销
> - [[文件IO与mmap]] — 资源下载与本地缓存

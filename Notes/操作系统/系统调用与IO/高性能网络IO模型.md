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

> [[索引|← 返回 系统调用与IO索引]]

# 高性能网络 IO 模型

---

## Why：为什么游戏服务器需要高性能网络 IO？

- **MMO / 大世界游戏**：单服需支撑数万到数十万并发连接。
- **实时性要求**：状态同步、帧同步对延迟敏感，网络层不能成为瓶颈。
- **C10K / C10M 问题**：传统阻塞模型无法支撑海量连接，需要事件驱动和非阻塞 IO。

> **历史背景：C10K 问题**
> 1999 年，Dan Kegel 提出 **C10K 问题**（Concurrent 10,000 connections）：如何在一台普通服务器上同时处理 10,000 个并发连接？当时的主流方案是"一连接一线程"，但 Linux 上线程栈默认 8MB，10,000 个连接就需要 80GB 虚拟内存——这完全不可行。C10K 问题催生了事件驱动编程模型和非阻塞 IO 多路复用技术（epoll、kqueue、IOCP）。到 2010 年代，随着移动互联网爆发，C10M 问题（千万并发）又被提出，推动了 DPDK、io_uring 等新技术。

---

## What：核心模型

### 阻塞 IO（Blocking IO）

- 一连接一线程。线程在 `recv` 时阻塞等待数据。
- 简单，但线程数随连接数线性增长，上下文切换开销巨大。

### 非阻塞 IO + 多路复用

- **select / poll**：遍历所有 fd 检查状态，fd 数量大时效率低（O(n)）。

> **select 的 fd_set 位图限制**：select 使用固定大小的位图（fd_set）来表示要监听的文件描述符集合。在 Linux 上，默认 `FD_SETSIZE` 是 1024，意味着 select 最多只能监听 1024 个 fd。修改这个宏需要重新编译内核或 glibc，极不灵活。此外，select 每次调用都要把完整的 fd_set 从用户态拷贝到内核态，再拷贝回来——O(n) 的遍历 + 两次拷贝，在连接数大时效率极差。

- **epoll（Linux）**：内核维护就绪队列，用户只拿到活跃的 fd（O(1)）。支持 ET（Edge Triggered，边缘触发）和 LT（Level Triggered，水平触发）。

> **epoll 的内核实现**：epoll 在内核中维护了两颗核心数据结构：
> 1. **红黑树（RB-Tree）**：存储所有注册到 epoll 的 fd 及其关注的事件。红黑树保证插入、删除、查找都是 O(log n)。
> 2. **就绪链表（Ready List）**：当某个 fd 上有事件发生时，内核将其加入就绪链表。`epoll_wait()` 只需把就绪链表中的 fd 拷贝到用户态，无需遍历全部 fd。
> 
> 这种设计使得 epoll 在连接数很大但只有少数活跃时，效率远超 select/poll。

- **IOCP（Windows）**：完成端口（I/O Completion Port），真正的异步 IO。提交 IO 请求后继续执行，完成后内核将结果放入完成端口队列，工作线程从队列中取出结果。

> **IOCP 与 epoll 的本质区别**：epoll 是**事件通知**（"这个 fd 可读/可写了"），应用程序仍需自己调用 `read()`/`write()` 执行 IO；IOCP 是**完成通知**（"这个 read 操作已完成，数据在这里"），IO 本身由内核异步执行。从 Proactor 模式角度看，IOCP 更纯粹。

- **io_uring（Linux 5.1+）**：共享环形缓冲区，用户态提交 SQE（Submission Queue Entry），内核完成回填 CQE（Completion Queue Entry），减少 syscall。

> **io_uring 的环形缓冲区机制**：io_uring 在**用户态和内核态之间共享一片内存**，包含两个环形队列：
> - **SQ（Submission Queue）**：用户程序把 IO 请求（如 read、write、accept）封装成 SQE 放入 SQ。用户通过修改内存中的 tail 指针提交，无需 syscall。
> - **CQ（Completion Queue）**：内核完成 IO 后，将结果封装成 CQE 放入 CQ。用户通过检查 head 指针即可获取结果，无需 syscall。
> 
> 在批量 IO 场景下，用户甚至可以一次提交多个 SQE（`io_uring_enter` 批量提交），将 syscall 次数从"每个 IO 一次"降到"每批 IO 一次"。对于游戏服务器的高频网络 IO，这是巨大的优化。

### ET（Edge Triggered）vs LT（Level Triggered）

epoll 支持两种触发模式，这是使用 epoll 时最容易踩的坑：

| 特性 | LT（Level Triggered，水平触发） | ET（Edge Triggered，边缘触发） |
|------|-------------------------------|-------------------------------|
| **触发时机** | 只要 fd 处于可读/可写状态，每次 `epoll_wait()` 都会返回 | 仅在 fd 从"不可读"变为"可读"的瞬间触发一次 |
| **编程要求** | 简单，`epoll_wait()` 返回后读一次数据即可 | **必须一次性读完所有数据**（读到 `EAGAIN`），否则剩余数据永远收不到 |
| **性能** | 可能重复触发（数据没读完，下次还会通知） | 触发次数最少，效率更高 |
| **易用性** | 不易出错 | 极易出错，稍不注意就漏事件 |

> **为什么 ET 要求必须读完？** 因为 ET 只在"状态变化边缘"触发。假设 socket 收到 100 字节，ET 触发一次。如果你只读了 50 字节，剩下 50 字节还在内核缓冲区里——但 fd 的状态仍是"可读"（没有从不可读变为可读），所以 ET 不会再触发。LT 则会因为"仍处于可读状态"而持续返回。
>
> **游戏服务器建议**：除非你对 epoll 非常熟悉且有性能极致需求，否则使用 LT 更安全。ET 的 bug 通常极难调试（表现为"偶尔收不到数据"）。

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

### 零拷贝（Zero-Copy）

传统文件发送流程（如游戏服务器发送资源文件给客户端）：
1. `read(fd, buf, len)` — 内核从磁盘读数据到**内核 Page Cache**。
2. 数据从内核 Page Cache 拷贝到**用户态缓冲区 buf**。
3. `send(socket, buf, len)` — 数据从用户态 buf 拷贝到**内核 Socket 发送缓冲区**。
4. 内核将 Socket 缓冲区数据拷贝到网卡 DMA 区域。

**总共 4 次拷贝，2 次 syscall，2 次用户态/内核态切换**。

**零拷贝技术**通过绕过用户态缓冲区，减少拷贝次数：

| 技术 | 原理 | 适用场景 |
|------|------|----------|
| **`sendfile()`** | 内核直接将文件内容从 Page Cache 拷贝到 Socket 缓冲区，无需经过用户态 | 发送静态文件（如 HTTP 服务器） |
| **`splice()`** | 在内核中直接将数据从一个 fd "拼接"到另一个 fd，零拷贝 | 管道与 socket 之间的数据转发 |
| **`mmap() + write()`** | 文件映射到用户态地址空间，write 时内核直接从映射页拷贝到 socket | 需要访问文件内容时 |

> 游戏服务器中，补丁下载、地图资源分发等场景非常适合使用 `sendfile()` 或 `splice()` 实现零拷贝，大幅降低 CPU 占用。

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

### 实验 2：Linux epoll Echo Server（完整实现）

```cpp
// workspace/os-lab/syscall/epoll_echo.cpp (Linux)
// g++ epoll_echo.cpp -o epoll_echo
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <vector>

constexpr int MAX_EVENTS = 1024;
constexpr int PORT = 7777;

void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main() {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    set_nonblocking(listen_fd);

    int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(listen_fd, (sockaddr*)&addr, sizeof(addr));
    listen(listen_fd, 128);

    int epoll_fd = epoll_create1(0);
    epoll_event ev{}, events[MAX_EVENTS];
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev);

    std::cout << "epoll Echo Server on port " << PORT << "\n";

    while (true) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd == listen_fd) {
                // 新连接
                int conn_fd = accept(listen_fd, nullptr, nullptr);
                set_nonblocking(conn_fd);
                ev.events = EPOLLIN | EPOLLET; // ET 模式
                ev.data.fd = conn_fd;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_fd, &ev);
                std::cout << "New connection: " << conn_fd << "\n";
            } else {
                // 数据可读
                int fd = events[i].data.fd;
                char buf[1024];
                int n = recv(fd, buf, sizeof(buf), 0);
                if (n <= 0) {
                    // 连接关闭或错误
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                    close(fd);
                    std::cout << "Connection closed: " << fd << "\n";
                } else {
                    send(fd, buf, n, 0); // echo
                }
            }
        }
    }

    close(epoll_fd);
    close(listen_fd);
    return 0;
}
```

**关键要点**：
1. **非阻塞模式**：`fcntl(O_NONBLOCK)` 保证 `accept()`/`recv()` 不会阻塞主循环。
2. **ET 模式**：`EPOLLET` 减少触发次数，但需注意新连接也要用循环 `accept()` 直到 `EAGAIN`。
3. **错误处理**：`recv() <= 0` 时关闭 fd 并从 epoll 中移除，避免监听已关闭的 fd。

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

---
title: 网络基础：Socket、TCP/IP 与协议栈
date: 2026-04-19
tags:
  - 操作系统
  - 网络
  - TCP/IP
  - Socket
aliases:
  - Network Basics Socket TCP IP
---

> [← 返回 系统调用与IO索引]([[索引\|系统调用与IO索引]])

# 网络基础：Socket、TCP/IP 与协议栈

---

## Why：为什么要理解网络基础？

- **游戏服务器的根基**：MMO、大世界游戏、帧同步/状态同步都建立在可靠或高效的网络通信之上。不理解 Socket 和 TCP/IP，就无法设计合理的网络协议和选型 IO 模型。
- **从"黑盒"到"白盒"**：很多开发者把网络通信视为"发数据和收数据的管道"。当遇到延迟抖动、丢包、粘包、连接异常断开时，只有理解协议栈的分层设计和 TCP 的状态机，才能系统性地定位和解决问题。
- **性能优化的前提**：为什么 UDP 比 TCP 更适合实时游戏？为什么 epoll 比 select 快？为什么零拷贝能减少 CPU 负载？这些问题的答案都建立在对网络协议栈的理解之上。

---

## What：网络通信的分层世界

### OSI 七层模型 vs TCP/IP 四层模型

网络协议采用**分层设计**，每层只与相邻层交互，屏蔽下层复杂性。

| OSI 七层 | TCP/IP 四层 | 典型协议/数据单元 | 作用 |
|----------|-------------|-------------------|------|
| 应用层 | **应用层** | HTTP、FTP、DNS、自定义游戏协议 | 程序直接交互的接口 |
| 表示层 | （合并） | TLS/SSL 加密、数据序列化 | 数据格式转换、加密 |
| 会话层 | （合并） | NetBIOS、RPC 会话管理 | 连接会话管理 |
| 传输层 | **传输层** | **TCP**、**UDP**、SCTP | 端到端的可靠/不可靠传输 |
| 网络层 | **网络层** | **IP**、ICMP、IGMP | 跨网络寻址与路由 |
| 数据链路层 | **链路层** | 以太网（Ethernet）、Wi-Fi、ARP | 局域网内帧传输、MAC 寻址 |
| 物理层 | （合并） | 光纤、双绞线、无线电波 | 比特流的物理传输 |

> **关键洞察**：作为应用程序员（包括游戏引擎和服务器开发），我们主要与**应用层**和**传输层**打交道。但理解网络层（IP 路由）和链路层（MAC 地址、ARP）有助于诊断"为什么连不上服务器""为什么延迟突然增加"等实际问题。

### IP 地址与端口：定位一台主机上的一个程序

- **IP 地址（网络层）**：标识互联网上的唯一主机。IPv4 是 32 位（如 `192.168.1.1`），IPv6 是 128 位。IP 解决的是"数据包送到哪台机器"。
- **端口号（传输层）**：16 位整数（0~65535），标识主机上的具体进程。端口解决的是"数据包交给哪个程序"。
  - **well-known 端口**：0~1023，需管理员权限，如 HTTP=80、HTTPS=443。
  - **注册端口**：1024~49151，如 MySQL=3306、Redis=6379。
  - **动态/私有端口**：49152~65535，客户端程序临时使用。

> 游戏服务器通常监听一个固定端口（如 7777），每个客户端连接时操作系统为其分配一个临时端口（如 54321）。一个 TCP 连接由**四元组**唯一标识：`(源IP, 源端口, 目的IP, 目的端口)`。

### TCP：可靠传输的代价

TCP（Transmission Control Protocol）提供**面向连接、可靠、有序、字节流**的传输服务。其核心机制包括：

#### 三次握手（建立连接）

```
客户端                    服务器
   │     SYN=1, seq=x      │
   │ ────────────────────> │
   │   SYN=1, ACK=1, seq=y, ack=x+1 │
   │ <──────────────────── │
   │     ACK=1, seq=x+1, ack=y+1    │
   │ ────────────────────> │
   │      [ESTABLISHED]    │
```

> **为什么不是两次？** 防止历史重复连接初始化造成的混乱，并同步双方初始序列号。若只有两次握手，客户端无法确认服务器是否真正收到了自己的 SYN。

#### 四次挥手（断开连接）

```
客户端                    服务器
   │      FIN=1, seq=u     │
   │ ────────────────────> │
   │      ACK=1, ack=u+1   │
   │ <──────────────────── │
   │   [服务器仍有数据要发]  │
   │      FIN=1, seq=w     │
   │ <──────────────────── │
   │      ACK=1, ack=w+1   │
   │ ────────────────────> │
   │      [CLOSED]         │
```

> **为什么不是三次？** 因为 TCP 是全双工的，一方发完 FIN 只表示"我不再发数据了"，但还能接收。服务器可能还有数据要发给客户端，所以 ACK 和 FIN 不能合并。

#### TCP 的可靠性机制

| 机制 | 作用 | 对实时游戏的影响 |
|------|------|-----------------|
| **序列号与确认应答（ACK）** | 确保数据不丢失、按序到达 | 丢包时触发重传，造成延迟波动 |
| **滑动窗口** | 流量控制，避免发送方淹没接收方 | 高延迟链路下窗口扩张慢，吞吐受限 |
| **拥塞控制** | 慢启动、拥塞避免、快速重传 | 网络抖动时主动降速，加剧延迟 |
| **Nagle 算法** | 小数据包合并，减少报文数量 | 增加延迟，实时游戏通常关闭（`TCP_NODELAY`） |

> **游戏开发者的权衡**：TCP 的可靠性和有序性对状态同步（如 MMO 位置更新）是负担——一个旧包的丢失会导致后续所有包阻塞等待重传。因此实时游戏常选择 **UDP + 自定义可靠性层**（如 ENET、KCP），或 UDP + 冗余发送（同一帧数据发 3 次，只要收到一次即可）。

### Socket API：操作系统提供的网络编程接口

Socket（套接字）是操作系统对网络通信端点的抽象。核心 API 流程：

**服务器端**：
1. `socket()` — 创建套接字，指定协议族（IPv4/IPv6）和类型（TCP/UDP）。
2. `bind()` — 将套接字绑定到本地 IP 和端口。
3. `listen()` — 开始监听传入连接，指定 backlog（等待队列长度）。
4. `accept()` — 阻塞等待，返回一个新的套接字代表与客户端的连接。
5. `recv()` / `send()` — 在该连接上读写数据。

**客户端**：
1. `socket()` — 创建套接字。
2. `connect()` — 向服务器 IP:端口发起连接（三次握手在此发生）。
3. `send()` / `recv()` — 读写数据。

> **文件描述符（File Descriptor, FD）**：在 Linux/Unix 中，Socket 被当作一种"文件"来管理，`socket()` 返回的是一个整数 FD。操作系统维护一个**文件描述符表**，每个进程独立。FD 0=标准输入、1=标准输出、2=标准错误，从 3 开始是用户打开的文件和 Socket。Windows 中 Socket 是 `SOCKET` 类型（本质是指针句柄），不是文件 FD，但概念类似。

### 阻塞 vs 非阻塞 Socket

| 模式 | 行为 | 特点 |
|------|------|------|
| **阻塞（Blocking）** | `accept()`/`recv()` 无数据时线程挂起，直到有数据 | 简单，但一连接一线程，连接数大时线程爆炸 |
| **非阻塞（Non-blocking）** | `recv()` 立即返回，无数据时返回 `EAGAIN`/`EWOULDBLOCK` | 需配合多路复用（select/epoll）轮询可读状态 |

> 游戏服务器通常采用**非阻塞 + 多路复用**模式，单线程管理数万连接。

---

## How：如何实践？

### 实验：最小 TCP Echo 客户端/服务器

```cpp
// workspace/os-lab/socket/tcp_echo_minimal.cpp
// 跨平台最小 TCP Echo Server + Client
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#include <iostream>
#include <cstring>
#include <thread>
#include <chrono>

void init_network() {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
}

void cleanup_network() {
#ifdef _WIN32
    WSACleanup();
#endif
}

void run_server() {
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8888);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(listenfd, (sockaddr*)&addr, sizeof(addr));
    listen(listenfd, 5);
    std::cout << "[Server] Listening on 8888...\n";

    int connfd = accept(listenfd, nullptr, nullptr);
    std::cout << "[Server] Client connected\n";

    char buf[1024];
    while (true) {
        int n = recv(connfd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break;
        buf[n] = '\0';
        std::cout << "[Server] Received: " << buf << "\n";
        send(connfd, buf, n, 0); // echo back
    }

#ifdef _WIN32
    closesocket(connfd);
    closesocket(listenfd);
#else
    close(connfd);
    close(listenfd);
#endif
}

void run_client() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8888);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(sockfd, (sockaddr*)&addr, sizeof(addr)) != 0) {
        std::cerr << "[Client] Connect failed\n";
        return;
    }
    std::cout << "[Client] Connected\n";

    const char* msg = "Hello, TCP!";
    send(sockfd, msg, strlen(msg), 0);

    char buf[1024];
    int n = recv(sockfd, buf, sizeof(buf) - 1, 0);
    if (n > 0) {
        buf[n] = '\0';
        std::cout << "[Client] Echo: " << buf << "\n";
    }

#ifdef _WIN32
    closesocket(sockfd);
#else
    close(sockfd);
#endif
}

int main() {
    init_network();
    std::thread server(run_server);
    std::thread client(run_client);
    server.join();
    client.join();
    cleanup_network();
    return 0;
}
```

**预期结果**：服务器启动监听，客户端连接并发送 "Hello, TCP!"，服务器打印接收内容并回传，客户端打印回传内容。

---

## 游戏引擎延伸

### 状态同步 vs 帧同步的网络需求

| 同步方式 | 网络层选择 | 原因 |
|----------|-----------|------|
| **状态同步**（FPS、MOBA、MMO） | UDP 为主 + 可靠性层 | 服务器以固定频率广播世界状态，旧状态过期无价值，TCP 重传旧包无意义 |
| **帧同步**（RTS、格斗游戏） | UDP | 只同步玩家输入，客户端确定性模拟。对延迟极度敏感，TCP 拥塞控制不可接受 |
| **文件传输/补丁下载** | TCP | 需要完整性，延迟不敏感 |

### Socket 缓冲区与引擎设计

操作系统为每个 Socket 维护**发送缓冲区**和**接收缓冲区**：
- `send()` 不是直接发网卡，而是把数据拷贝到内核发送缓冲区，内核择机发送。
- 若发送缓冲区满，`send()` 在阻塞模式下会等待，非阻塞模式下返回 `EAGAIN`。

引擎网络层通常实现一个**应用层发送队列**，当操作系统缓冲区满时，数据暂存在用户态队列中，避免阻塞游戏主循环。

---

> [!info] 延伸阅读
> - [[高性能网络IO模型]] — 在理解 Socket 和 TCP/IP 后，深入 select/poll/epoll/IOCP/io_uring 的实现与选型
> - [[系统调用与vDSO]] — 网络 IO 中 syscall 的开销分析

---
title: 文件IO与虚拟文件系统
date: 2026-04-15
tags:
  - self-game-engine
  - foundation
  - io
  - vfs
aliases:
  - File IO and VFS
---

> **前置依赖**：[[引擎基础类型与平台抽象]]、[[字符串系统]]、[[容器与分配器]]
> **本模块增量**：为引擎提供跨平台文件读写、路径抽象、虚拟文件系统挂载和基础配置解析能力，使引擎能加载外部资源与配置。
> **下一步**：[[线程池与任务系统]] — 文件 IO 与异步任务结合，才能支撑资源的异步加载；或 [[资源管理]] — VFS 是资源系统的底层支柱。

# 文件 IO 与虚拟文件系统

## Why：为什么引擎需要自己的文件 IO 层？

直接用 `std::ifstream` 和 `fopen` 不行吗？对于一个小 demo 也许可以，但当引擎需要同时支持开发期迭代、打包发布、MOD 扩展和多平台部署时，标准库的零散工具会迅速崩溃：

- **路径分隔符混乱**：Windows 用 `\`，Linux/macOS 用 `/`。硬编码路径字符串在跨平台时会直接失效。
- **开发期 vs 发布期路径不一致**：开发时资源在 `../../assets/` 里，发布后资源被打包进 `.pak` 或嵌入可执行文件。没有虚拟文件系统（VFS），同一段加载代码无法同时适应两种环境。
- **同步 IO 阻塞主线程**：在主线程直接 `fread` 一张 4K 纹理，帧时间会突然 spike 到几十毫秒，画面直接卡顿。
- **配置格式碎片化**：有的模块用 JSON，有的用 XML，有的用 ini。没有统一的解析入口，配置加载代码变成一锅粥。

### 三个真实场景

| 场景 | 没有 VFS 会怎样 | 后果 |
|------|----------------|------|
| 美术修改了一张纹理 | 需要重新打包整个资源包才能让程序看到变化 | 迭代周期从秒级变成分钟级 |
| 玩家安装了 MOD | MOD 文件和原版文件混在一起，无法优先级覆盖 | MOD 系统几乎不可实现 |
| 从 Steam 下载了 DLC | 引擎不知道新的资源目录该挂载到哪里 | DLC 内容无法加载 |

**结论**：VFS 不是"锦上添花"，而是引擎从"玩具"走向"产品"的必经之路。

---

## What：最小文件 IO + VFS 长什么样？

下面给出约 120 行的最小实现，包含：跨平台路径拼接、同步文件读取、VFS 挂载优先级、以及一个极简 JSON 配置解析入口。

### 1. Path 抽象

```cpp
#include <cstring>

class Path {
    SmallString m_path;
public:
    Path() = default;
    Path(const char* p) : m_path(p) {}
    const char* c_str() const { return m_path.c_str(); }

    Path operator/(const char* sub) const {
        Path result = *this;
        const char* str = result.m_path.c_str();
        usize len = result.m_path.length();
        if (len > 0 && str[len-1] != '/' && str[len-1] != '\\') {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "%s/%s", str, sub);
            return Path(buf);
        }
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s%s", str, sub);
        return Path(buf);
    }

    const char* Extension() const {
        const char* str = m_path.c_str();
        const char* dot = std::strrchr(str, '.');
        return dot ? dot + 1 : "";
    }
};
```

> **这段代码已经解决了什么**：统一使用 `/` 作为分隔符（运行时无需关心平台差异）、提取文件扩展名、路径拼接不重复斜杠。
> **还缺什么**：没有规范化 `.` 和 `..`、没有绝对/相对路径判断、没有宽字符（Unicode）支持。

---

### 2. 同步文件读取 + VFS 挂载

```cpp
#include <cstdio>
#include <vector>
#include <memory>

// 原始字节缓冲区（配合 [[容器与分配器]] 的 FrameArena 使用更佳）
struct FileData {
    std::vector<u8> bytes;
    bool valid = false;
};

// 单个档案源（本地目录或 Pak 包）
class IArchive {
public:
    virtual ~IArchive() = default;
    virtual FileData ReadFile(const char* path) = 0;
    virtual bool Exists(const char* path) = 0;
};

// 本地文件系统档案
class FileSystemArchive : public IArchive {
    Path m_root;
public:
    explicit FileSystemArchive(const char* root) : m_root(root) {}

    FileData ReadFile(const char* path) override {
        FileData result;
        Path full = m_root / path;
        FILE* f = std::fopen(full.c_str(), "rb");
        if (!f) return result;
        std::fseek(f, 0, SEEK_END);
        long size = std::ftell(f);
        std::rewind(f);
        result.bytes.resize(size);
        std::fread(result.bytes.data(), 1, size, f);
        std::fclose(f);
        result.valid = true;
        return result;
    }

    bool Exists(const char* path) override {
        Path full = m_root / path;
        FILE* f = std::fopen(full.c_str(), "rb");
        if (f) { std::fclose(f); return true; }
        return false;
    }
};

// 虚拟文件系统：按优先级从高到低查询多个档案源
class VFS {
    std::vector<std::unique_ptr<IArchive>> m_archives;
public:
    void Mount(std::unique_ptr<IArchive> archive) {
        m_archives.push_back(std::move(archive));
    }

    FileData ReadFile(const char* path) {
        for (auto& archive : m_archives) {
            if (archive->Exists(path)) {
                return archive->ReadFile(path);
            }
        }
        return FileData{};
    }

    bool Exists(const char* path) {
        for (auto& archive : m_archives) {
            if (archive->Exists(path)) return true;
        }
        return false;
    }
};
```

**VFS 查询状态图**：两个 Archive 的优先级查询

```
Mount 顺序：
1. Mount("../../assets/")   ← 开发期本地目录（优先级最高）
2. Mount("game.pak")        ← 发布期 Pak 包（回退）

ReadFile("textures/hero.png"):
   ├─> FileSystemArchive("../../assets/") 是否存在？
   │      ✅ 存在 → 读取并返回
   │      ❌ 不存在
   │
   └─> PakArchive("game.pak") 是否存在？
          ✅ 存在 → 读取并返回
          ❌ 不存在 → 返回空文件
```

> **这段代码已经解决了什么**：开发期优先读取本地文件（热重载），缺失时回退到 Pak 包；发布期直接挂载 Pak 包即可。
> **还缺什么**：没有异步读取接口、没有文件修改时间查询（热重载检测）、没有写文件能力、没有目录遍历。

---

### 3. 极简 JSON 配置解析入口

配置解析器不需要从零手写（除非你想学习状态机）。对于 vibe coding 阶段，直接集成一个单头文件的第三方库（如 `jsonh`/`rapidjson` 的单头简化版）是最高效的。下面是基于假设的极简 JSON API 的引擎封装：

```cpp
// 假设你引入了一个单头 JSON 库，提供如下 C 风格接口：
// json_t* json_parse(const char* text);
// double  json_number(json_t* obj, const char* key);
// const char* json_string(json_t* obj, const char* key);

struct Config {
    static bool LoadFloat(VFS& vfs, const char* path, const char* key, f32& out) {
        auto file = vfs.ReadFile(path);
        if (!file.valid || file.bytes.empty()) return false;
        file.bytes.push_back('\0'); // 确保以 null 结尾
        // 这里调用你集成的 JSON 解析器
        // json_t* root = json_parse(reinterpret_cast<char*>(file.bytes.data()));
        // out = static_cast<f32>(json_number(root, key));
        // 为示例，我们直接模拟一个简单解析：
        const char* text = reinterpret_cast<const char*>(file.bytes.data());
        const char* k = std::strstr(text, key);
        if (!k) return false;
        const char* colon = std::strchr(k, ':');
        if (!colon) return false;
        out = static_cast<f32>(std::atof(colon + 1));
        return true;
    }
};
```

> **诚实说明**：上面的 `Config::LoadFloat` 是一个**概念骨架**，只演示"从 VFS 读取 → 解析 → 输出变量"的链路。真实项目中，请集成一个经过验证的 JSON/TOML 解析器（如 `nlohmann/json` 的单头版、`tomlc99`）。

---

## How：真实引擎的 IO 系统是如何一步一步复杂起来的？

### 阶段 1：直接用 fopen → 能用但有坑

刚开始只有一个资源目录，直接用 `fopen("assets/hero.png", "rb")` 读取。一切简单直接。

但问题很快暴露：
- 路径硬编码，跨平台编译失败
- 发布时需要手动复制资源目录到可执行文件旁边
- 没有异步，加载大资源时画面卡死

### 阶段 2：VFS + 异步 IO → 好用

**触发原因**：开发期需要热重载、发布期需要 Pak 包、玩家需要 MOD 支持。

**代码层面的变化**：

1. **引入 VFS**：`IArchive` 抽象让本地目录、Pak 包、内存档案、网络档案统一接口。
2. **引入 Pak 包**：将多个小文件打包成一个二进制文件，减少文件句柄数和磁盘寻道时间。Pak 格式通常自描述（文件末尾带目录表），无需外部 catalog。
3. **引入异步加载器**：后台线程（见 [[线程池与任务系统]]）负责 `fread`，主线程通过回调或 future 获取结果。

```cpp
// 异步加载的伪代码
ResourceHandle LoadTextureAsync(VFS& vfs, const char* path) {
    ResourceHandle handle = AllocateHandle();
    threadPool.Submit([&vfs, path, handle]() {
        FileData data = vfs.ReadFile(path);
        // 在后台线程解码 PNG/ DDS
        TextureData* tex = DecodeTexture(data);
        // 将 GPU 上传任务投递回主线程
        mainThreadQueue.Push([handle, tex]() {
            UploadToGPU(handle, tex);
        });
    });
    return handle;
}
```

#### 热重载（Hot Reload）

开发期挂载 `FileSystemArchive` 到 `../../assets/`。引擎每帧检查关键文件的 `mtime`（最后修改时间），如果发生变化，自动重新加载并替换运行中的资源。这让美术和策划的修改能在 1 秒内反映在屏幕上。

### 阶段 3：平台后端抽象 + 写保护 + 序列化集成 → 工业级

**触发原因**：引擎需要在主机（PS/Xbox/Switch）、移动端、PC 上同时发布，且需要支持存档、配置、用户生成内容（UGC）。

1. **平台文件后端**：封装 `std::fstream`（通用）、`POSIX`（Linux）、`Win32 API`（Windows 的 `CreateFileW` 支持长路径和异步 IO）、`I/O Kit`（主机）。VFS 的 `IArchive` 下增加 `PlatformFileBackend` 层。
2. **只读 vs 可写路径分离**：
   - **只读**：Pak 包、原始资源、引擎内置数据。
   - **可写**：玩家存档、截图、日志、下载的 DLC、MOD 数据。
   - 不同平台对"可写路径"的规定不同（如 Windows 的 `%APPDATA%`、iOS 的 `Documents` 目录）。
3. **序列化集成**：[[反射系统]] 生成每个组件的 Schema 后，序列化器可以自动将 ECS 世界保存为 JSON/Binary，VFS 负责将其写入可写路径。加载时通过 VFS 读取并反序列化。

---

## AI 友好设计红线检查

| 检查项 | 结论 | 说明 |
|--------|------|------|
| **状态平铺** | ✅ 是 | VFS 本身不持有复杂的运行时状态，只有 Archive 列表和路径映射，完全可序列化描述 |
| **自描述** | ✅ 是 | AI 可以查询 VFS 的挂载列表和文件存在性，不需要读 C++ 头文件 |
| **确定性** | ⚠️ 需注意 | 文件 IO 的时序（尤其是异步加载）可能引入非确定性。应在 [[系统调度与确定性]] 中规定：所有资源必须在帧开始前的固定阶段完成加载，运行时禁止随机 IO |
| **工具边界** | ✅ 是 | AI 通过 MCP 操作 VFS 时，接口非常清晰：`mount(path)`、`read_file(path)`、`exists(path)`。输入输出都是结构化字符串 |
| **Agent 安全** | ⚠️ 需限制 | AI 不应被允许向任意路径写文件（防止覆盖存档或注入恶意代码）。写操作必须限制在引擎的"可写沙箱目录"内 |

---

## 性能与 Trade-off：诚实分析

| 设计点 | 优势 | 代价 | 反例警告 |
|--------|------|------|----------|
| VFS 优先级查询 | 开发期热重载、MOD 覆盖、Pak 回退三位一体 | 每次读取需要遍历 Archive 列表，N 个 Archive 最坏 O(N) | 如果挂载了 20+ 个 Archive（如大量 MOD），查询开销会累积。应引入路径→Archive 的缓存索引 |
| Pak 包 | 减少文件句柄、减少磁盘寻道、便于加密 | 单个文件损坏可能导致整个 Pak 无法读取；热重载粒度变粗 | 开发期不要过度依赖 Pak，否则美术修改一张图就要重新打包 |
| 同步 fread | 代码简单、时序可预测 | 阻塞调用线程，大文件导致帧时间 spike | 任何 > 1MB 的资源读取都应走异步路径 |

---

## 下一步预告

文件 IO 就位后，引擎终于可以：
- 从外部加载配置文件
- 读取纹理、网格、音频等二进制资源
- 在开发期和发布期使用同一套加载代码

但同步文件读取会阻塞主线程。接下来：

- **[[线程池与任务系统]]**：把 `fread` 和纹理解码放到后台线程，主线程只负责最终的 GPU 提交和状态更新。

只有异步加载到位，引擎才能在加载 4K 纹理时保持 60 FPS。

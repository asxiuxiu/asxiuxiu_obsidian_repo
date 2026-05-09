---
title: 文件IO与虚拟文件系统
date: 2026-05-09
tags:
  - self-game-engine
  - foundation
  - io
  - vfs
aliases:
  - File IO and VFS
---

> **前置依赖**：[[引擎基础类型与平台抽象]]、[[字符串系统]]、[[容器系统]]
> **本模块增量**：为引擎提供跨平台文件读写、路径抽象、虚拟文件系统挂载、Pak 归档与基础配置解析能力。读完这篇笔记后，你的引擎将能：统一开发期/发布期的资源加载路径、支持 MOD/DLC 覆盖、加载打包资源、以及为后续异步资源系统奠定底层支柱。
> **下一步**：[[线程池与任务系统]] — 文件 IO 与异步任务结合，才能支撑资源的异步加载；或 [[资源管理]] — VFS 是资源系统的底层支柱。

# 文件 IO 与虚拟文件系统

## 问题 0：为什么引擎不能直接用 `fopen`？

如果你写过一个命令行小工具，加载配置文件大概是这样的：

```cpp
FILE* f = fopen("assets/config.json", "rb");
```

这行代码在单文件 demo 里运行良好，但一旦进入游戏引擎的语境，它会同时撞上四面墙。

**第一面墙：跨平台路径**。Windows 用 `\`，Linux 和 macOS 用 `/`。你在 Windows 上写了 `"textures\\hero.png"`，到 Linux 上直接找不到文件。更隐蔽的是，Windows 的 API 分 ANSI 版（`CreateFileA`）和宽字符版（`CreateFileW`），长路径或中文文件名在 ANSI 路径下会直接失败。

**第二面墙：开发期与发布期的路径不一致**。开发时美术资源散落在 `../../assets/` 下的几百个文件夹里；发布时这些文件被打包成一个或数个 `.pak` 二进制包，甚至可能嵌入到可执行文件内部。没有虚拟文件系统（VFS），同一段加载代码必须写两套：`#ifdef DEBUG` 读目录，`#else` 读 Pak。

**第三面墙：同步 IO 阻塞主线程**。在主线程直接 `fread` 一张 4K 纹理，如果此时磁盘在忙其他事，16ms 的帧预算可能被吃掉 8ms，画面直接从 60fps 掉到 45fps。玩家感受到的是一次明显卡顿。

**第四面墙：MOD、DLC、用户生成内容（UGC）无从下手**。玩家装了一个 MOD，MOD 文件和原版文件混在一起，引擎不知道"这个路径该优先读 MOD 还是读原版"。从 Steam 下载的 DLC 新增了一批资源，引擎也不知道"新的资源目录该挂载到哪里"。

> **场景绑定**：想象你的游戏支持创意工坊。玩家订阅了 20 个 MOD，每个 MOD 覆盖不同的皮肤、音效和关卡。如果没有 VFS，你需要在加载代码里手动维护 20 个 `if (modX_enabled)` 分支——这几乎不可维护。

所以，**VFS 不是"锦上添花"，而是引擎从"玩具"走向"产品"的必经之路**。它的核心使命只有一句话：**让上层代码只关心"我要读哪个逻辑路径"，不关心"这个路径背后到底是本地目录、Pak 包、内存缓冲区还是网络下载"。**

---

## 问题 1：如何把"物理位置"和"逻辑路径"解耦？

### 最 naive 的方案：宏定义路径前缀

```cpp
#ifdef DEBUG
    #define ASSET_PREFIX "../../assets/"
#else
    #define ASSET_PREFIX "game.pak/"
#endif

FILE* f = fopen(ASSET_PREFIX "textures/hero.png", "rb");
```

这个方案能跑，但立刻暴露三个问题：
1. **新增一种发布形态就要新增一个宏分支**。比如你想支持"Steam 创意工坊目录优先"，就得再写一个 `#ifdef STEAM_WORKSHOP`。
2. **运行时无法动态调整**。宏在编译期就确定了，运行时玩家安装了一个新 MOD，宏帮不了你。
3. **路径拼接靠字符串操作**。`"../../assets/" + "textures/hero.png"` 在 C++ 里就是一场手动管理缓冲区的灾难。

### 改进：Path 抽象

我们需要一个类型来封装"路径"这个概念，让它自动处理分隔符、拼接、扩展名提取等琐事。

```cpp
class Path {
    String m_path;  // 使用引擎自定义的 String（参考 [[字符串系统]]）
public:
    Path() = default;
    explicit Path(const char* p);

    // 路径拼接：自动处理重复的斜杠
    Path operator/(const char* sub) const {
        Path result = *this;
        const char* str = result.m_path.c_str();
        usize len = result.m_path.length();
        if (len > 0 && str[len-1] != '/' && str[len-1] != '\\') {
            return Path(String::format("%s/%s", str, sub));
        }
        return Path(String::format("%s%s", str, sub));
    }

    const char* Extension() const;
    const char* FileName() const;
    const char* Stem() const;
    bool IsAbsolute() const;
};
```

> **注意**：这里内部统一用 `/` 作为分隔符存储，只在最终调用 OS API 时才根据平台转换为 `\`。这样 `"assets/textures/hero.png"` 在 Windows 和 Linux 上表示的是同一个逻辑路径。

Path 抽象解决了字符串拼接的混乱，但没有解决"同一个逻辑路径在不同环境下指向不同物理位置"的问题。于是我们需要进一步引入**虚拟文件系统**。

---

## 问题 2：VFS 如何支持多种后端？（本地目录、Pak、内存、网络）

### 最 naive 的方案：if-else 判断

```cpp
FileData ReadFile(const char* path) {
    if (g_use_pak) {
        return ReadFromPak(path);
    } else {
        return ReadFromDisk(path);
    }
}
```

问题很明显：**新增后端要改所有调用方**。如果你想加一个"从内存缓存读取"的模式，或者支持"网络按需下载"，每个 `ReadFile` 的调用点都要被翻出来修改。这违反了开闭原则。

### 改进：MountPoint（挂载点）抽象

VFS 的核心思想来自 Unix 的 mount 命令：把不同的物理存储源"挂载"到同一个逻辑命名空间的某个位置上。上层代码看到的始终是一棵统一的目录树，不知道也不关心某个节点背后是什么。

```cpp
// 一个挂载点能回答两个问题："这个路径我有没有？" "有的话怎么读？"
class IMountPoint {
public:
    virtual ~IMountPoint() = default;
    virtual bool Exists(const char* path) = 0;
    virtual FileData ReadFile(const char* path) = 0;
    virtual bool CanWrite() const { return false; }
    virtual bool WriteFile(const char* path, Slice<u8> data) { return false; }
};

// 本地文件系统挂载点
class FileSystemMountPoint : public IMountPoint {
    Path m_root;
public:
    explicit FileSystemMountPoint(const char* root) : m_root(root) {}

    FileData ReadFile(const char* path) override {
        FileData result;
        Path full = m_root / path;
        // 平台抽象的文件读取（见下方 PlatformFileBackend）
        result.bytes = PlatformReadFile(full.c_str());
        result.valid = !result.bytes.empty();
        return result;
    }

    bool Exists(const char* path) override {
        Path full = m_root / path;
        return PlatformFileExists(full.c_str());
    }
};

// 内存挂载点（用于测试或嵌入资源）
class MemoryMountPoint : public IMountPoint {
    HashMap<String, Array<u8>> m_files;
public:
    void Register(const char* path, Slice<u8> data) {
        m_files[path] = Array<u8>(data.data, data.data + data.size);
    }

    FileData ReadFile(const char* path) override {
        auto it = m_files.find(path);
        if (it == m_files.end()) return FileData{};
        return FileData{ it->second, true };
    }

    bool Exists(const char* path) override {
        return m_files.contains(path);
    }
};
```

### VFS：按优先级查询的挂载管理器

```cpp
class VFS {
    Array<UniquePtr<IMountPoint>> m_mounts;
public:
    void Mount(UniquePtr<IMountPoint> mount) {
        m_mounts.push_back(std::move(mount));
    }

    FileData ReadFile(const char* path) {
        // 优先级：先挂载的优先（或反过来，按需求定）
        for (auto& mount : m_mounts) {
            if (mount->Exists(path)) {
                return mount->ReadFile(path);
            }
        }
        return FileData{};  // 未找到
    }

    bool Exists(const char* path) {
        for (auto& mount : m_mounts) {
            if (mount->Exists(path)) return true;
        }
        return false;
    }
};
```

**VFS 查询状态图**：

```
Mount 顺序（优先级从高到低）：
1. Mount("mods/cool_skins/")    ← 玩家 MOD（最高优先级）
2. Mount("dlc/new_levels/")     ← DLC 内容
3. Mount("../../assets/")       ← 开发期本地目录
4. Mount("game.pak")            ← 发布期 Pak 包（回退）

ReadFile("textures/hero.png"):
   ├─> FileSystemMountPoint("mods/cool_skins/") 是否存在？
   │      ✅ 存在 → 读取并返回（MOD 覆盖生效）
   │      ❌ 不存在
   │
   ├─> FileSystemMountPoint("dlc/new_levels/") 是否存在？
   │      ✅ 存在 → 读取并返回
   │      ❌ 不存在
   │
   ├─> FileSystemMountPoint("../../assets/") 是否存在？
   │      ✅ 存在 → 读取并返回（开发期热重载）
   │      ❌ 不存在
   │
   └─> PakMountPoint("game.pak") 是否存在？
          ✅ 存在 → 读取并返回
          ❌ 不存在 → 返回空文件
```

> **场景绑定**：开发期挂载本地目录到 `../../assets/`，美术改一张图保存后，引擎立刻读到新版本。发布期只挂载 Pak 包，无需修改任何加载代码。玩家装了 MOD，MOD 目录优先挂载，原版资源自动被覆盖。

**但这里还遗留了一个问题**：如果挂载了 20+ 个 MountPoint（比如大量 MOD），每次 `Exists` 都要线性遍历，最坏 O(N)。

**改进：路径 → MountPoint 的缓存索引**

```cpp
class VFS {
    Array<UniquePtr<IMountPoint>> m_mounts;
    HashMap<String, IMountPoint*> m_path_cache;  // 缓存命中过的路径
public:
    FileData ReadFile(const char* path) {
        auto it = m_path_cache.find(path);
        if (it != m_path_cache.end()) {
            return it->second->ReadFile(path);
        }
        for (auto& mount : m_mounts) {
            if (mount->Exists(path)) {
                m_path_cache[path] = mount.get();  // 缓存结果
                return mount->ReadFile(path);
            }
        }
        return FileData{};
    }

    // 新增/卸载 MountPoint 时清空缓存
    void InvalidateCache() { m_path_cache.clear(); }
};
```

> **代价说明**：缓存增加了内存占用（HashMap 存储每个命中路径的指针），且在挂载/卸载时需要显式失效。对于 MOD 数量不多的单机游戏，线性遍历通常已足够；只有在大量动态挂载场景下才需要此缓存。

---

## 问题 3：如何高效读取 Pak 归档？（减少文件句柄、降低寻道）

### 最 naive 的方案：每个资源独立存储

开发期的 `assets/` 目录下有 3000 个零散文件。发布时直接把这些文件复制到安装目录。运行时打开 3000 个文件句柄，操作系统很快就吃不消了。更糟的是，机械硬盘上的随机小文件读取意味着大量寻道时间，加载一个关卡可能需要几十秒。

### 改进：Pak 包格式

Pak 的核心思想是**把多个小文件合并成少量大文件**，在包尾部放置一个"目录表"（TOC, Table of Contents），记录每个文件的逻辑路径、偏移量和大小。这样运行时只需要打开几个 Pak 文件句柄，通过目录表直接 seek 到数据位置。

一个极简 Pak 的内存布局：

```
+-----------------------+
|  文件 0 数据           |  ← 偏移 0
|  文件 1 数据           |
|  文件 2 数据           |
|  ...                   |
+-----------------------+
|  目录表（TOC）          |  ← 记录每个文件的：逻辑路径、数据偏移、大小、压缩/加密标志
+-----------------------+
|  TOC 偏移量（8 字节）   |  ← 文件末尾，指向目录表起始位置
+-----------------------+
```

读取 Pak 内文件的流程：

```cpp
class PakMountPoint : public IMountPoint {
    struct FileEntry {
        String path;
        u64 offset_in_pak;
        u64 size;
        u32 compressed_size;  // 0 表示未压缩
        u32 flags;            // 压缩/加密标志位
    };

    Array<u8> m_pak_data;           // 若内存映射，指向映射地址
    Array<FileEntry> m_entries;     // 目录表
    HashMap<String, u32> m_path_to_index;

public:
    bool Load(const char* pak_path) {
        // 1. 打开 Pak 文件
        // 2. 读取末尾 8 字节，得到 TOC 偏移
        // 3. seek 到 TOC，读取所有 FileEntry
        // 4. 构建 HashMap 加速查找
    }

    FileData ReadFile(const char* path) override {
        auto it = m_path_to_index.find(path);
        if (it == m_path_to_index.end()) return FileData{};

        const FileEntry& entry = m_entries[it->second];
        Array<u8> buffer;
        buffer.resize(entry.size);

        // 从 Pak 文件的 entry.offset 位置读取 entry.size 字节
        PlatformSeekAndRead(pak_path, entry.offset_in_pak, buffer.data(), entry.size);

        if (entry.flags & kCompressed) {
            buffer = Decompress(buffer);  // 如 LZ4 解压
        }
        if (entry.flags & kEncrypted) {
            buffer = Decrypt(buffer);     // 如 AES 解密
        }

        return FileData{ std::move(buffer), true };
    }
};
```

**但这里有一个关键问题**：如果 Pak 包里存放的是大量小文件（比如几百个 1KB 的配置文件），每次读取都要一次 seek + 读取 + 解压，开销反而可能比独立文件更大。

**改进：内联 Payload（小文件优化）**

在目录表的每个 `FileEntry` 中增加一个 `payload` 字段：如果文件体积小于某个阈值（比如 4KB），直接把文件数据嵌入到目录表里，而不是放到前面的数据区。读取时无需二次 seek，直接从内存中的目录表拷贝数据。

```cpp
struct FileEntry {
    // ... 原有字段
    u32 payload_size;
    Array<u8> payload;  // 小文件内联数据
};

FileData ReadFile(const char* path) {
    // ... 找到 entry
    if (entry.payload_size > 0) {
        // 直接拷贝内联数据，零 seek
        return FileData{ Array<u8>(entry.payload), true };
    }
    // 否则走常规 seek 读取
}
```

> **参考：工业界的 Pak 设计**
>
> 对于「Pak 归档的具体格式设计」这个子问题，不同引擎有不同的取舍：
> - **chaos 引擎**采用尾部自描述格式（20 字节加密指针指向目录表），并引入 Catalog 包机制——用一个额外的 `catalog_pkg_0.pkg` 记录所有子包的命名规则和索引，避免运行时遍历目录。热更包通过末尾的 `hot_patch_index` 排序，后加载的包覆盖先加载的包。Chunk 级支持 LZ4 压缩 + AES-128-CBC 加密的三种组合。
> - **UE 的 FPakFile** 同样在尾部存放目录表，但支持 Mount 优先级和 Pak 之间的覆盖层级。UE 的 Pak 还内置了压缩（Oodle/Zlib）和加密（AES-256）选项，并允许在 Cook 阶段按平台选择不同的压缩策略。
> - **Bevy 不直接使用 Pak**，而是通过 `AssetSource` 抽象支持文件系统、网络、内存等多种来源。Bevy 的资产在发布时通常以目录形式存在，依赖 `AssetProcessor` 做预处理（压缩、格式转换），输出到 `imported_assets/` 目录。
>
> **个人项目推荐**：默认采用"尾部目录表 + LZ4 压缩 + 小文件内联"的 Pak 格式。AES 加密在需要防拆包时可选，但会增加 CPU 开销；对于个人项目或开源游戏，加密往往不是首要需求。

---

## 问题 4：开发期美术改了一张图，怎么让引擎立刻看到？

### 最 naive 的方案：重启引擎

美术保存了一张新纹理，程序员重启程序才能看到效果。迭代周期从秒级变成分钟级，创意流程被打断。

### 改进：热重载（Hot Reload）

热重载的本质是**在运行时检测文件变化，并重新加载受影响的资源**。VFS 层的热重载通常分两步走：

**第一步：检测变化**。开发期挂载的 `FileSystemMountPoint` 需要能报告文件的修改时间。

```cpp
class FileSystemMountPoint : public IMountPoint {
    HashMap<String, u64> m_last_modified_times;
public:
    void PollChanges(Array<String>& out_changed_files) {
        // 遍历挂载目录下的关键文件（或借助 OS 文件监控 API）
        // 对比 mtime，有变化就加入 out_changed_files
    }
};
```

**第二步：重新加载并通知上层**。检测到 `textures/hero.png` 变化后，VFS 重新读取文件，但问题并没有结束——上层可能已经持有了旧纹理的 GPU 资源句柄，单纯替换 VFS 中的字节流不会让屏幕上的画面更新。

这里需要一个**脏 Handle 机制**：VFS 或资源管理器标记某个资源的 Handle 为"已过期"，上层系统每帧检查脏 Handle 列表并刷新对应的 GPU 资源。

```cpp
class VFS {
    HashMap<String, FileData> m_hot_reload_cache;
    HashSet<String> m_dirty_files;
public:
    // 每帧调用（仅开发期）
    void UpdateHotReload() {
        for (auto& mount : m_mounts) {
            if (auto* fs = dynamic_cast<FileSystemMountPoint*>(mount.get())) {
                Array<String> changed;
                fs->PollChanges(changed);
                for (const auto& path : changed) {
                    m_hot_reload_cache[path] = fs->ReadFile(path.c_str());
                    m_dirty_files.insert(path);
                }
            }
        }
    }

    // 供上层查询"哪些文件自上次检查后变了"
    HashSet<String> GetAndClearDirtyFiles() {
        HashSet<String> result = std::move(m_dirty_files);
        m_dirty_files.clear();
        return result;
    }
};
```

> **参考：Bevy 的热重载与事件驱动**
>
> Bevy 的热重载采用了更彻底的事件驱动架构。文件系统变更由 `notify-debouncer-full` 监控，产生 `AssetSourceEvent::ModifiedAsset`，最终转化为 `AssetEvent::Modified` 的 ECS Message。渲染系统监听 `AssetEvent<Image>`，事件到达时自动更新 `GpuImage`。这种设计的优势是**上层完全不需要主动轮询脏标记**，ECS 的 Query Filter（`AssetChanged<Image>`）会自动过滤出引用已变更资产的实体。
>
> 对于我们的 C++ ECS 引擎，可以借鉴这个思路：VFS 的热重载不直接操作 GPU 资源，而是生成 ECS 事件（如 `FileChangedEvent { path }`），由专门的 System 消费事件并执行实际的资源刷新。这保持了 VFS 的纯粹性，也让 AI 能观察到"哪些文件在何时发生了变化"。

---

## 问题 5：资源之间有依赖关系，怎么保证加载顺序正确？

### 最 naive 的方案：按文件名顺序加载

假设一个材质文件 `hero.mat` 内部引用了纹理 `diffuse.png` 和法线贴图 `normal.png`。如果你先加载 `hero.mat`，解析时发现 `diffuse.png` 还没加载，要么崩溃，要么显示粉色缺省纹理。

### 改进：依赖图与拓扑排序

每个资源在加载完成后，应该能明确报告"我依赖哪些其他资源"。资源管理器维护一张依赖图，确保**叶子资源（无依赖）先加载，被依赖的资源在依赖方之前就绪**。

```cpp
struct LoadRequest {
    String path;
    Array<String> dependencies;  // 加载完成后解析出的依赖列表
    bool ready = false;          // 自身数据是否已加载
};

class DependencyGraph {
    HashMap<String, LoadRequest> m_nodes;
    HashMap<String, HashSet<String>> m_dependents;  // 反向图：谁依赖我

public:
    void AddNode(const String& path, Array<String> deps) {
        m_nodes[path] = LoadRequest{ path, std::move(deps), false };
        for (const auto& dep : m_nodes[path].dependencies) {
            m_dependents[dep].insert(path);
        }
    }

    void MarkReady(const String& path) {
        m_nodes[path].ready = true;
        // 检查依赖我的那些节点，看它们是否全部就绪
        auto it = m_dependents.find(path);
        if (it == m_dependents.end()) return;

        for (const auto& dependent : it->second) {
            auto& node = m_nodes[dependent];
            bool all_deps_ready = true;
            for (const auto& dep : node.dependencies) {
                if (!m_nodes[dep].ready) {
                    all_deps_ready = false;
                    break;
                }
            }
            if (all_deps_ready && !node.ready) {
                // 所有依赖已就绪，可以正式激活这个资源
                ActivateResource(dependent);
            }
        }
    }
};
```

**状态变化图**：

```
初始状态：
  hero.mat  → 依赖 [diffuse.png, normal.png]  → ready=false
  diffuse.png → 依赖 []                       → ready=false
  normal.png  → 依赖 []                       → ready=false

加载 diffuse.png 完成后：
  diffuse.png → ready=true
  检查 dependents["diffuse.png"] = {hero.mat}
  hero.mat 的依赖中 normal.png 仍 ready=false → 不激活

加载 normal.png 完成后：
  normal.png → ready=true
  检查 dependents["normal.png"] = {hero.mat}
  hero.mat 的所有依赖都 ready=true → 激活 hero.mat
```

> **参考：chaos 与 Bevy 的依赖处理**
>
> - **chaos 引擎**在 `AssetManager` 的后处理线程中维护了一棵 `LoadingTree`。加载线程完成 IO 和反序列化后，将结果投递到后处理线程；后处理线程通过有向图的"叶子消褪"算法判断何时可以安全地解析资源引用。这种双线程分离的设计让磁盘 IO 不被引用解析阻塞。
> - **Bevy** 的依赖追踪更加自动化。`#[derive(Asset)]` 会通过过程宏自动生成 `VisitAssetDependencies` trait 的实现，遍历资产结构体中所有 `Handle<T>` 字段，自动收集依赖关系。`AssetInfo` 中同时维护 `loading_dependencies`（我在等谁）和 `dependents_waiting_on_load`（谁在等我），形成双向链表，状态传播时无需轮询。
>
> **个人项目推荐**：初期采用显式依赖声明（如材质文件头部列出引用的纹理路径），资源加载器解析文件后填充依赖图。当反射系统（阶段 4.4）就位后，可以升级为自动依赖扫描——遍历反序列化后的组件/资产对象，发现 `Handle<Texture>` 字段就自动注册依赖。

---

## 问题 6：大文件加载阻塞主线程怎么办？

### 最 naive 的方案：同步 fread

主线程直接调用 `VFS::ReadFile("textures/4k_skybox.dds")`，`fread` 阻塞直到 64MB 数据全部读入内存。如果此时操作系统缓存未命中，磁盘 DMA 需要几十毫秒，玩家看到一次明显卡顿。

### 改进：异步加载管线

VFS 本身只负责"如何读"，不负责"什么时候读"。但一个完整的异步加载管线需要 VFS 提供**非阻塞的读取接口**，以及上层资源系统配合调度。

在我们的引擎中，VFS 的同步接口已经足够支撑异步管线——关键是把 `ReadFile` 的调用放到后台线程中：

```cpp
// 在线程池中提交加载任务（详见 [[线程池与任务系统]]）
Future<FileData> LoadAsync(VFS& vfs, const char* path) {
    return g_thread_pool.Submit([&vfs, path]() {
        return vfs.ReadFile(path);
    });
}
```

但这里隐藏着一个更深层的问题：**不同后端的异步行为不一致**。本地文件系统的 `ReadFile` 在后台线程执行时不会阻塞主线程；但如果某个 MountPoint 背后是网络下载，"读取"操作本身可能需要数秒，且涉及连接建立、重试、超时等复杂逻辑。

**改进：MountPoint 暴露异步能力**

```cpp
class IMountPoint {
public:
    // 同步读取（所有 MountPoint 必须实现）
    virtual FileData ReadFile(const char* path) = 0;

    // 异步读取（可选实现，默认退化为同步在线程池中执行）
    virtual Future<FileData> ReadFileAsync(const char* path) {
        return g_thread_pool.Submit([this, path]() {
            return this->ReadFile(path);
        });
    }
};
```

> **参考：chaos 的双线程模型**
>
> chaos 引擎的 `AssetManager` 使用了**加载线程 + 后处理线程**的分离模型：
> - **加载线程**：从 Pak/文件系统读取原始字节，调用反序列化器生成对象，将结果放入无锁队列。
> - **后处理线程**：消费队列中的已加载对象，解析引用关系（Handle → 实际对象指针），触发回调。
> - **主线程**：几乎零阻塞，只在每帧开始时检查已完成的任务。
>
> 这种分离让磁盘吞吐和 CPU 密集型反序列化可以并行，后处理线程的拓扑排序也不会被慢速 IO 拖住。

---

## 问题 7：AI 如何理解和操作文件系统？

我们的引擎目标明确：**为 AI 协作而设计**。VFS 作为所有资源的入口，必须回答三个问题：

### AI 如何观察它？

VFS 的状态应该是**平铺、可序列化、自描述的**。AI 不需要读 C++ 头文件就能知道：
- 当前挂载了哪些 MountPoint？
- 某个逻辑路径是否存在？
- 某个文件最近一次热重载是什么时候？

```cpp
// AI Schema 导出示例（JSON 格式）
{
    "vfs_state": {
        "mounts": [
            { "type": "FileSystem", "root": "mods/cool_skins/", "priority": 0 },
            { "type": "Pak", "path": "game.pak", "priority": 3 }
        ],
        "file_exists": {
            "textures/hero.png": true,
            "textures/villain.png": false
        }
    }
}
```

### AI 如何操作它？

如果 AI 通过 MCP/AgentBridge 操作 VFS，接口应该是结构化的、有边界的：

| 操作 | 输入 | 输出 | 安全限制 |
|------|------|------|----------|
| `vfs.read_file(path)` | 逻辑路径 | 字节数组或文本 | 只读，任意路径 |
| `vfs.write_file(path, data)` | 逻辑路径 + 数据 | 成功/失败 | **仅限可写沙箱目录** |
| `vfs.mount(type, physical_path)` | 挂载类型 + 物理路径 | 成功/失败 | 禁止挂载到系统敏感路径 |
| `vfs.list_dir(path)` | 逻辑路径 | 文件列表 | 只读 |

> **Agent 安全红线**：AI 绝不应被允许向任意路径写文件。写操作必须限制在引擎的"可写沙箱目录"内（如 `%APPDATA%/MyEngine/saves/` 或 `./user_data/`），防止覆盖存档、注入恶意代码或破坏系统文件。

### ECS 映射

在 ECS 架构下，VFS 的挂载列表和运行时状态可以作为**全局 Resource**（或 World 级别的单例组件）存在：

```cpp
// VFS 作为 ECS Resource
struct VFSResource {
    VFS vfs;
};

// 热重载 System：每帧检查文件变化
void HotReloadSystem(World& world, Resource<VFSResource> vfs) {
    auto changed = vfs->vfs.GetAndClearDirtyFiles();
    for (const auto& path : changed) {
        world.send_event(FileChangedEvent{ path });
    }
}

// 资源刷新 System：消费事件并更新 GPU 资源
void ResourceRefreshSystem(World& world, Query<Handle<Texture>&> textures) {
    for (auto event : world.read_events<FileChangedEvent>()) {
        // 查找引用该路径的 Texture 组件，触发 GPU 重上传
    }
}
```

这种设计让 VFS 完全融入了 ECS 的数据驱动模型：文件变化是事件，资源刷新是 System，AI 可以通过查询 World 中的 Resource 和 Event 来理解整个文件系统的状态。

---

## 工业级设计清单

| 设计点 | 最小实现（阶段 3） | 生产级演进（阶段 8） |
|--------|------------------|-------------------|
| 路径抽象 | `Path` 类，统一 `/` 分隔符 | 规范化 `.`/`..`、宽字符支持、UNC 路径 |
| VFS 挂载 | 线性遍历 MountPoint | 路径缓存索引、运行时动态挂载/卸载 |
| Pak 格式 | 尾部目录表 + 可选 LZ4 | Chunk 级压缩/加密、Catalog 包、内联 Payload |
| 热重载 | 轮询 mtime | OS 文件监控（ReadDirectoryChangesW/inotify）、事件驱动 |
| 依赖管理 | 显式声明依赖图 | 自动依赖扫描（反射遍历 Handle 字段） |
| 异步加载 | 线程池 + Future | 双线程分离（IO + 后处理）、优先级队列、流送（mip 级） |
| AI 可观测 | 手动导出挂载列表 | 自动 Schema 生成、Change Log、操作审计 |

---

## 性能与 Trade-off：诚实分析

| 设计点 | 优势 | 代价 | 反例警告 |
|--------|------|------|----------|
| VFS 优先级查询 | 开发期热重载、MOD 覆盖、Pak 回退三位一体 | 每次读取需遍历 MountPoint 列表，N 个 Archive 最坏 O(N) | 挂载 20+ 个 MOD 时查询开销累积，应引入路径缓存索引 |
| Pak 包 | 减少文件句柄、减少磁盘寻道、便于加密 | 单个 Pak 损坏可能导致大量资源丢失；热重载粒度变粗 | 开发期不要过度依赖 Pak，美术修改一张图要重新打包全量 Pak |
| 同步 `ReadFile` | 代码简单、时序可预测、无回调地狱 | 阻塞调用线程，大文件导致帧时间 spike | 任何 > 1MB 的资源读取都应走异步路径 |
| 内联 Payload | 小文件零 seek，读取极快 | 增加目录表内存占用 | 阈值设置过高（如 64KB）会导致 Pak 膨胀，加载大量内联数据时反而拖慢启动 |
| 热重载 mtime 轮询 | 实现简单、跨平台 | 有延迟（轮询间隔）、持续消耗 CPU | 轮询间隔设成 1 秒，美术保存后最快 1 秒才生效；间隔太短又浪费性能 |

---

## 引擎对照总结

我们在解决的是「引擎如何从磁盘读取资源，并在不同环境下保持行为一致」这个问题。以下是三家引擎在这一领域的核心差异：

| 子问题 | chaos 引擎 | Unreal Engine | Bevy |
|--------|-----------|---------------|------|
| **路径抽象** | `String` 路径 + `FileID`（哈希标识） | `FPaths` 规范化 + `FString` | `AssetPath<'a>` 三元组（source/path/label） |
| **归档格式** | `.pkg` 尾部目录表 + Chunk 压缩/加密 | `.pak` 尾部 TOC + 平台特化 Cook | 目录为主，`AssetProcessor` 预处理后输出 |
| **挂载策略** | ArchiveManager 缓存 + 工厂注册 | `FPakFile` Mount 优先级 | `AssetSourceBuilders` 多来源注册 |
| **异步模型** | 双后台线程（加载+后处理） | Async Loading + Streaming | `IoTaskPool` + `crossbeam_channel` 事件同步 |
| **热重载** | mtime 比对 + Dirty Handle | 编辑器自动重编译/重载 | `notify` 文件监控 + `AssetEvent` ECS Message |
| **依赖追踪** | LoadingTree 拓扑排序 | `FStreamableManager` 依赖链 | `VisitAssetDependencies` 自动宏推导 + 双向依赖图 |
| **引用管理** | Handle 体系 + PossessedHandle RAII | `TSharedPtr` / `FWeakObjectPtr` | `Handle::Strong` channel-based drop + `AssetIndexAllocator` |

**个人项目推荐路径**：
1. **阶段 3（当前）**：实现 `Path` + `IMountPoint` + `VFS` + 极简 Pak 格式（尾部 TOC，无压缩）。同步读取，支持开发期本地目录优先挂载。
2. **阶段 5（资源管理）**：在 VFS 之上引入异步加载管线（线程池 + Future），支持 Handle 引用和资源依赖图。
3. **阶段 7（编辑器）**：接入 OS 文件监控实现事件驱动热重载，导出 VFS Schema 供 AI 观察。
4. **阶段 8（扩展）**：升级 Pak 支持 LZ4 压缩、Catalog 包、Chunk 级加密；引入加载优先级和纹理 mip 流送。

---

## 回探替换清单

完成本模块后，回探替换阶段 1 的以下临时实现：

| 阶段 1 临时代码 | 替换为 | 验证方法 |
|----------------|--------|----------|
| 日志文件输出（裸 `fopen`） | `VFS::WriteFile("logs/engine.log", data)` | 日志正常写入，且通过 VFS 挂载可重定向到不同目录 |
| 硬编码资源路径（如 `"../../assets/"`） | `Path` + VFS 挂载 | 同一套加载代码在开发期（本地目录）和发布期（Pak）都能工作 |

---

## 下一步预告

文件 IO 就位后，引擎终于可以：
- 从外部加载配置文件和原始资源
- 在开发期和发布期使用同一套加载代码
- 支持 MOD 覆盖和基础热重载

但同步文件读取会阻塞主线程，资源之间的依赖关系也需要更系统的管理。接下来：

- **[[线程池与任务系统]]**：把 `ReadFile` 和纹理解码放到后台线程，主线程只负责最终的 GPU 提交和状态更新。只有异步加载到位，引擎才能在加载 4K 纹理时保持 60 FPS。

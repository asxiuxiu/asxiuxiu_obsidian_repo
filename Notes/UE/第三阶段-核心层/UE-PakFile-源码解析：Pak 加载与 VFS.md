---
title: UE-PakFile-源码解析：Pak 加载与 VFS
date: 2026-04-17
tags:
  - ue-source
  - engine-architecture
  - pakfile
  - vfs
aliases:
  - UE-PakFile
---

> [← 返回 00-UE全解析主索引]([[00-UE全解析主索引\|UE全解析主索引]])

## Why：为什么要理解 PakFile 与 VFS？

UE 的发布包不会将成千上万的 `.uasset` 文件以松散文件形式分发，而是打包成 `.pak` 文件。Pak 文件不仅是简单的归档容器，它还是 UE 的**虚拟文件系统（VFS）**核心，支持压缩、加密、签名验证、补丁覆盖、异步预读等高级特性。理解 Pak 文件的格式结构、挂载流程和 IO 拦截机制，是分析资源加载、Cook 打包、热更新、Chunk 安装等上层功能的必经之路。

## What：PakFile 模块是什么？

`PakFile` 模块实现了 `.pak` 文件的读写解析和 VFS 挂载。它的核心组件包括：

1. **Pak 格式解析**：`FPakFile` 负责读取 Pak 文件尾部索引，解析文件条目（`FPakEntry`）。
2. **VFS 挂载层**：`FPakPlatformFile` 继承 `IPlatformFile`，拦截所有文件 IO，优先查询已挂载的 Pak。
3. **安全与性能**：支持 AES 加密、RSA 签名验证、压缩块解码、Precacher 异步预读。

### 核心类定位

| 类 | 职责 |
|---|---|
| `FPakInfo` | Pak 尾部元数据：Magic、Version、IndexOffset/Size、IndexHash、加密/压缩方法表 |
| `FPakEntry` | 单个文件条目：Offset、Size、UncompressedSize、Hash、CompressionBlocks、Flags |
| `FPakEntryLocation` | 索引定位器：编码字节偏移或 Files 数组下标 |
| `FPakFile` | Pak 文件对象：加载索引、维护 MountPoint、Find()、GetSharedReader() |
| `FPakFileHandle` | 继承 `IFileHandle`，负责 Seek/Read，按需解密/解压缩 |
| `FPakPlatformFile` | 继承 `IPlatformFile`，拦截所有文件 IO，实现 Pak-First 策略 |
| `FPakPrecacher` | 异步预缓存：按 64KB 粒度管理 Pak 块的异步读取、解密、签名校验 |

## How：PakFile 的三层源码剖析

### 第 1 层：接口层（What）

#### FPakInfo 的尾部元数据结构

> 文件：`Engine/Source/Runtime/PakFile/Public/IPlatformFilePak.h`，第 137~200 行

```cpp
struct FPakInfo
{
    enum { PakFile_Magic = 0x5A6F12E1 };

    uint32 Magic;
    int32 Version;
    int64 IndexOffset;
    int64 IndexSize;
    FSHAHash IndexHash;
    uint8 bEncryptedIndex;
    FGuid EncryptionKeyGuid;
    TArray<FName> CompressionMethods;
};
```

`FPakInfo` 位于 Pak 文件**最末尾**。运行时 `FPakFile::Initialize()` 会先从文件尾部读取 `FPakInfo`，然后根据 `IndexOffset` 和 `IndexSize` 加载索引区。版本回退检测机制会依次尝试旧版本尾部大小，保证向后兼容。

#### FPakPlatformFile 的 VFS 接口

> 文件：`Engine/Source/Runtime/PakFile/Private/IPlatformFilePak.cpp`（接口声明在 `IPlatformFilePak.h`）

```cpp
class PAKFILE_API FPakPlatformFile : public IPlatformFile
{
public:
    bool Mount(const FPakMountArgs& MountArgs, ...);
    bool FileExists(const TCHAR* Filename);
    int64 FileSize(const TCHAR* Filename);
    IFileHandle* OpenRead(const TCHAR* Filename, bool bAllowWrite = false);
    // ... 其他 IPlatformFile 接口

private:
    TArray<FPakListEntry> PakFiles;  // 按 ReadOrder 排序的 Pak 列表
    FPakPrecacher* Precacher;
};
```

`FPakPlatformFile` 将所有文件操作转换为 **"先查 Pak，再 fallback 到底层 OS 文件系统"** 的逻辑。

### 第 2 层：数据层（How - Structure）

#### Pak 文件格式布局

```
[文件数据块 ...] [索引区] [FPakInfo 尾部]
```

索引区（Version ≥ PathHashIndex）进一步分层：

| 索引 | 内容 |
|---|---|
| **PrimaryIndex** | MountPoint、NumEntries、PathHashSeed、EncodedPakEntries 字节数组、Files 列表 |
| **PathHashIndex** | `TMap<uint64, FPakEntryLocation>` — FNV64 文件名哈希到条目位置的映射 |
| **FullDirectoryIndex** | `TMap<FString, FPakDirectory>` — 完整目录树/文件名映射 |
| **PrunedDirectoryIndex** | 裁剪后的目录索引，用于运行时节省内存 |

#### FPakEntry 的内存优化编码

大多数 `FPakEntry` 会被 **bit-pack 编码**进 `EncodedPakEntries` 字节数组，显著降低内存占用。无法编码的（字段值超出范围）放入 `Files`（`TArray<FPakEntry>`）。`FPakEntryLocation` 用单一 `int32` 区分三种状态：编码字节偏移、Files 列表索引、Invalid。

#### FPakFileHandle 的读取流程

当上层调用 `OpenRead` 时：
1. `FPakPlatformFile` 遍历 `PakFiles`（按 `ReadOrder` 从高到低），调用 `FindFileInPakFiles()`。
2. 命中后获取 `FPakEntry`，创建 `FPakFileHandle`。
3. `FPakFileHandle::ReadInternal` → `FPakReaderPolicy::Serialize`：
   - 若 `CompressionMethodIndex != 0`，按 `CompressionBlocks` 边界切分解压缩。
   - 若加密，按 `EncryptionPolicy`（默认 AES）对齐解密。
   - 通过 `FSharedPakReader` 从底层 `FArchive` 读取数据到用户缓冲区。

### 第 3 层：逻辑层（How - Behavior）

#### Pak 挂载的完整调用链

```
FPakPlatformFile::Mount(MountArgs)
  ├── 打开 .pak 文件
  ├── FPakFile::Initialize()
  │     └── 读取尾部 FPakInfo
  │     └── LoadIndex()
  │           └── 读取 PrimaryIndex、PathHashIndex、FullDirectoryIndex
  ├── 密钥检查（若动态加密 KeyGuid 未注册则推迟挂载）
  ├── 计算 PakOrder（基础 order + 补丁文件 _P.pak 的 chunk 版本号加权）
  ├── IoStore 容器挂载（尝试挂载同名 .utoc）
  ├── 将 FPakListEntry 插入 PakFiles，按 ReadOrder 降序排序
  └── 广播 OnPakFileMounted2
```

**补丁覆盖逻辑**：`PakFiles` 按 `ReadOrder` 降序排列，高 order 的 Pak 中的同名文件会覆盖低 order 的 Pak。`_P.pak`（Patch Pak）通过更高的 order 实现热更新和增量补丁。

#### 文件 IO 拦截的 Pak-First 策略

| 操作 | 行为 |
|---|---|
| `FileExists` / `FileSize` / `OpenRead` | 先遍历 PakFiles（高 order 优先），命中即返回；未命中 fallback 到 OS |
| `OpenWrite` / `DeleteFile` / `MoveFile` | 若文件在 Pak 中，**直接拒绝**（Pak 只读） |
| `IterateDirectory` | 聚合所有 Pak 中的目录项，去重后 fallback 到 OS 松散文件 |

```
OpenRead(Filename)
  └── FindFileInPakFiles(Filename)
        └── for Pak in PakFiles (descending by ReadOrder)
              └── Pak.Find(Filename)
                    └── PathHashIndex.Find(FNV64Hash(Filename))
                          └── 返回 FPakEntryLocation
                                └── 解码为 FPakEntry
                                      └── new FPakFileHandle(Entry)
```

#### FPakPrecacher 的异步预读

`FPakPrecacher` 在 `FPakPlatformFile` 初始化时创建：
- 接收异步读请求（如纹理流送 `OpenAsyncRead`）。
- 将请求按 **64KB CacheBlock** 切分。
- 合并相邻请求，调度底层 `IAsyncReadFileHandle` 并发读取。
- 读取完成后执行解密和 CRC/SHA 签名校验。
- 支持优先级调度和取消，为渲染资源的异步流送提供低延迟 IO。

## 上下层关系

| 上层使用者 | 用法 |
|---|---|
| `AssetRegistry` | 查询 Pak 中资产的 Chunk 安装状态 |
| `StreamingFile` | 远程流式文件层（Cook-on-the-Fly） |
| `TextureStreaming` / `MeshStreaming` | 通过 `OpenAsyncRead` 异步加载大资源 |
| `Cooker` / `UnrealPak` | 生成 `.pak` 文件和签名文件 |

| 下层依赖 | 说明 |
|---|---|
| `Core` / `CoreUObject` | 基础类型 |
| `RSA` | 签名验证 |
| `IoStore` | `.utoc` / `.ucas` 容器挂载（UE5 新存储格式） |

## 设计亮点与可迁移经验

1. **尾部索引 + 快速挂载**：索引放在文件尾部，运行时只需 Seek 到末尾读取少量元数据，即可快速建立 VFS 映射，无需扫描整个 Pak。
2. **分层索引兼顾速度与内存**：`PathHashIndex` 用于 O(1) 文件查找，`FullDirectoryIndex` 用于目录遍历，`PrunedDirectoryIndex` 用于运行时内存优化。
3. **Bit-Pack 编码大幅降低内存**：大多数 `FPakEntry` 被编码进字节数组，相比原始结构体能节省大量内存，适合挂载大量 Pak 的场景。
4. **Order 驱动的补丁覆盖**：不修改原始 Pak，而是通过高 Order 的新 Pak 覆盖同名文件，实现了安全、可回滚的增量更新。
5. **Precacher 的异步块管理**：64KB 粒度的 CacheBlock 切分和合并，是现代游戏引擎处理大规模纹理/模型流送的标准范式。

## 关联阅读

- [[UE-Serialization-源码解析：Archive 序列化体系]]
- [[UE-AssetRegistry-源码解析：资产注册与发现]]
- [[UE-构建系统-源码解析：Pak 打包与 Zen 存储]]

## 索引状态

- **所属 UE 阶段**：第三阶段 - 核心层 / 3.2 序列化与数据层
- **对应 UE 笔记**：UE-PakFile-源码解析：Pak 加载与 VFS
- **本轮分析完成度**：✅ 已完成全部三层分析

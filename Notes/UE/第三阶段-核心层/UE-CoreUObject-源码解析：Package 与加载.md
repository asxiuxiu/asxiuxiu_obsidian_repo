---
title: UE-CoreUObject-源码解析：Package 与加载
date: 2026-04-17
tags:
  - ue-source
  - engine-architecture
aliases:
  - UE CoreUObject Package 加载
---

> [← 返回 UE全解析主索引]([[00-UE全解析主索引|UE全解析主索引]])

# UE-CoreUObject-源码解析：Package 与加载

## Why：为什么要深入理解 Package 加载？

Package（`.uasset` / `.umap`）是 UE 资源持久化的基本单元。从编辑器里的一个静态网格，到运行时加载的关卡地图，本质上都是一个 `UPackage`。理解 Package 的加载机制，是掌握 UE 资源管线、流送系统、热更新以及 Zen/IoStore 新架构的前提。

## What：Package 加载体系是什么？

UE 的 Package 加载体系采用 **"Linker 即 Archive"** 的设计哲学：
- **`UPackage`**：内存中的包对象，是所有持久化 UObject 的根 `Outer`。
- **`FPackageFileSummary`**：包文件顶部的"目录"，描述 NameMap、ImportMap、ExportMap 的偏移与数量。
- **`FLinkerLoad`**：同时是映射表管理器 + 序列化流，负责从磁盘解析包结构并填充 UObject。
- **加载流程**：`LoadPackage` → `GetPackageLinker` → `CreateLinker` → `Tick` → `LoadAllObjects` → `EndLoad`。

---

## 模块定位

- **UE 模块路径**：`Engine/Source/Runtime/CoreUObject/`
- **Build.cs 文件**：`CoreUObject.Build.cs`
- **核心依赖**：
  - `PublicDependencyModuleNames`：`Core`、`TraceLog`、`CorePreciseFP`
  - `PrivateDependencyModuleNames`：`AutoRTFM`、`Projects`、`Json`
- **关键目录**：
  - `Public/UObject/` — `Package.h`、`PackageFileSummary.h`、`LinkerLoad.h`
  - `Private/UObject/` — `Package.cpp`、`LinkerLoad.cpp`、`UObjectGlobals.cpp`
  - `Private/Serialization/` — `AsyncLoading2.cpp`（事件驱动加载器 EDL）

> 文件：`Engine/Source/Runtime/CoreUObject/CoreUObject.Build.cs`，第 26~44 行

```csharp
PublicDependencyModuleNames.AddRange(
    new string[]
    {
        "Core",
        "TraceLog",
        "CorePreciseFP",
    }
);
```

---

## 接口梳理（第 1 层）

### 核心头文件一览

| 头文件 | 核心类/结构 | 职责 |
|--------|------------|------|
| `Package.h` | `UPackage` | 内存中的包对象，管理 Linker、加载路径、保存接口 |
| `PackageFileSummary.h` | `FPackageFileSummary` | 包文件目录，存储各表的偏移和数量 |
| `Linker.h` | `FLinker` | `FLinkerLoad` / `FLinkerSave` 的公共基类 + 映射表容器 |
| `LinkerLoad.h` | `FLinkerLoad` | 核心加载器：读取包头、建立映射表、序列化 UObject |
| `LinkerSave.h` | `FLinkerSave` | 保存器：收集对象、构建映射表、写出磁盘 |
| `UObjectGlobals.h` | `LoadPackage`、`StaticLoadObject` | 全局加载 API |

### UPackage 关键接口

> 文件：`Engine/Source/Runtime/CoreUObject/Public/UObject/Package.h`

```cpp
class COREUOBJECT_API UPackage : public UObject
{
    GENERATED_BODY()
public:
    void SetLinker(FLinkerLoad* InLinker);
    FLinkerLoad* GetLinker() const;
    bool IsFullyLoaded() const;
    void FullyLoad();
    static bool SavePackage(...);
};
```

### FPackageFileSummary：包文件目录

> 文件：`Engine/Source/Runtime/CoreUObject/Public/UObject/PackageFileSummary.h`

| 关键字段 | 说明 |
|---------|------|
| `Tag` | 魔数 `PACKAGE_FILE_TAG` |
| `FileVersionUE` / `FileVersionLicenseeUE` | 序列化版本号 |
| `TotalHeaderSize` | 创建 FLinkerLoad 所需读取的总头大小 |
| `NameCount / NameOffset` | NameMap 位置和数量 |
| `ImportCount / ImportOffset` | ImportMap 位置 |
| `ExportCount / ExportOffset` | ExportMap 位置 |
| `DependsOffset` | Export 依赖关系表 |
| `BulkDataStartOffset` | BulkData 起始偏移 |

---

## 数据结构（第 2 层）

### FLinkerLoad 的双重身份

`FLinkerLoad` 继承自 `FLinker`（映射表容器）和 `FArchiveUObject`（序列化流）。

> 文件：`Engine/Source/Runtime/CoreUObject/Public/UObject/LinkerLoad.h`

```cpp
class FLinkerLoad : public FLinker, public FArchiveUObject
{
    FArchive* Loader;                    // 底层文件读取器
    uint32 LoadFlags;                    // LOAD_None / LOAD_Quiet 等
    // ...
public:
    ELinkerStatus Tick(float InTimeLimit, bool bInUseTimeLimit, ...);
    void LoadAllObjects(bool bForcePreload = false);
    void Preload(UObject* Object);
    UObject* CreateExport(int32 Index);
    UObject* CreateImport(int32 Index);
};
```

### Linker 的映射表体系

> 文件：`Engine/Source/Runtime/CoreUObject/Public/UObject/Linker.h`

```cpp
class FLinker : public FArchiveUObject
{
    UPackage* LinkerRoot;                        // 服务的根包
    FPackageFileSummary Summary;                 // 包摘要
    TArray<FNameEntryId> NameMap;                // 局部名称表
    TArray<FObjectImport> ImportMap;             // 导入表（依赖外部包）
    TArray<FObjectExport> ExportMap;             // 导出表（本包拥有的对象）
    TArray<FPackageIndex> PreloadDependencies;   // 预加载依赖图
};
```

### UObject 加载状态标志

| 标志 | 含义 |
|------|------|
| `RF_NeedLoad` | 对象壳已创建，但属性尚未从磁盘反序列化 |
| `RF_NeedPostLoad` | 等待 `PostLoad` 初始化阶段 |
| `RF_WasLoaded` | 该对象是通过加载而非运行时生成创建的 |

---

## 行为分析（第 3 层）

### 加载流程调用链

```
LoadPackage(UPackage*, FPackagePath, LoadFlags)
    └── LoadPackageInternal(...)
            ├── BeginLoad(LoadContext)
            ├── GetPackageLinker(...)
            │       └── FLinkerLoad::CreateLinker()
            │               ├── CreateLinkerAsync()   // 构造 FLinkerLoad
            │               └── Tick(0.f, ...)        // 阻塞读取 Summary + Maps
            ├── Linker->LoadAllObjects()
            │       └── CreateExportAndPreload(Index)
            │               ├── CreateExport(Index)   // StaticConstructObject 创建壳
            │               └── (可选) Preload(Object)
            └── EndLoad(LoadContext, &LoadedPackages)
                    ├── 阶段 A: Preload(Serialize)    // 对每个 RF_NeedLoad 对象调用 Serialize
                    ├── 阶段 B: PostLoad              // ConditionalPostLoad
                    ├── 阶段 C: PostLoadInstance      // 类级别的 PostLoad
                    └── 阶段 D: CreateCluster (Cook)
```

### 关键函数 1：FLinkerLoad::Tick

> 文件：`Engine/Source/Runtime/CoreUObject/Private/UObject/LinkerLoad.cpp`，第 1067 行附近

```cpp
ELinkerStatus FLinkerLoad::Tick(float InTimeLimit, bool bInUseTimeLimit, ...)
{
    // 1. 创建底层文件读取器 (FArchive)
    Status = CreateLoader(...);
    // 2. 读取并处理 FPackageFileSummary
    Status = ProcessPackageSummary(...);
    // 3. 后续 Tick 还会逐步序列化 NameMap、ImportMap、ExportMap、DependsMap 等
}
```

### 关键函数 2：EndLoad

> 文件：`Engine/Source/Runtime/CoreUObject/Private/UObject/UObjectGlobals.cpp`，第 2157 行附近

```cpp
while (DecrementBeginLoadCount() == 0 && HasLoadedObjects())
{
    // A. Preload (Serialize)
    AppendLoadedObjectsAndEmpty(ObjLoaded);
    Sort by Linker & File Offset;   // 磁盘顺序读取优化
    for (Obj : ObjLoaded)
        if (Obj->HasAnyFlags(RF_NeedLoad))
            Linker->Preload(Obj);   // 调用 UObject::Serialize()

    // B. PostLoad
    for (Obj : ObjLoaded)
        Obj->ConditionalPostLoad();

    // C. PostLoadInstance
    for (Obj : ObjLoaded)
        ObjClass->PostLoadInstance(Obj);
}
```

### 异步加载演进

| 版本 | 实现文件 | 特点 |
|------|---------|------|
| 传统 | `AsyncLoading.cpp` | 单线程/多线程混合，阻塞式 EndLoad |
| UE5 EDL | `AsyncLoading2.cpp` (~11k 行) | 事件驱动加载器，配合 IoDispatcher 实现真正的异步流送 |

---

## 上下层关系

### 上层调用者

| 模块 | 使用方式 |
|------|---------|
| `Engine` | `UWorld::LoadMap()` → `LoadPackage` 加载关卡包 |
| `AssetRegistry` | 解析资产依赖，指导 Package 预加载 |
| `StreamingFile` / `PakFile` | 提供 `FArchive` 底层读取，支持 VFS 和 Pak 包 |

### 下层依赖

| 模块 | 作用 |
|------|------|
| `Core` | 提供 `FArchive`、`FName`、`TArray` 等基础类型 |
| `Zen` / `IoStore` | UE5 新存储后端，`PackageStore.cpp` 集成 |

---

## 设计亮点与可迁移经验

1. **Linker 即 Archive**：`FLinkerLoad` 同时承担映射表管理和序列化流的双重角色，简化了"定位对象 → 读取属性"的接口。
2. **磁盘顺序读取优化**：`EndLoad` 中对 `ObjLoaded` 按 `SerialOffset` 排序，最大化利用磁盘缓存和预读。
3. **分阶段初始化**：`CreateExport`（建壳）→ `Preload`（读属性）→ `PostLoad`（初始化）的三段式，允许在加载过程中进行依赖解析和延迟初始化。
4. **异步加载的演进**：从"加载-等待"到"事件驱动-流送"，体现了大型引擎应对开放世界和资源规模膨胀的必然路径。

---

## 关键源码片段

### LoadPackageInternal 入口

> 文件：`Engine/Source/Runtime/CoreUObject/Private/UObject/UObjectGlobals.cpp`，第 1649 行附近

```cpp
UPackage* LoadPackageInternal(UPackage* InOuter, const FPackagePath& PackagePath, uint32 LoadFlags, ...)
{
    BeginLoad(LoadContext, ...);
    FLinkerLoad* Linker = GetPackageLinker(InOuter, PackagePath, LoadFlags, ...);
    if (Linker)
    {
        Linker->LoadAllObjects(bForcePreload);
    }
    EndLoad(LoadContext, &LoadedPackages);
    return InOuter;
}
```

### FLinkerLoad::Preload

> 文件：`Engine/Source/Runtime/CoreUObject/Private/UObject/LinkerLoad.cpp`，第 4694 行附近

```cpp
void FLinkerLoad::Preload(UObject* Object)
{
    if (Object->HasAnyFlags(RF_NeedLoad))
    {
        // 定位该对象在 ExportMap 中的 SerialOffset
        const FObjectExport& Export = ExportMap[ObjectIndex];
        Seek(Export.SerialOffset);
        // 将 FLinkerLoad 自身作为 FArchive 传入
        Object->Serialize(*this);
        Object->UnMark(EInternalObjectFlags::NeedLoad);
    }
}
```

---

## 关联阅读

- [[UE-CoreUObject-源码解析：UObject 体系总览]]
- [[UE-CoreUObject-源码解析：GC 与对象生命周期]]
- [[UE-Engine-源码解析：World 与 Level 架构]]
- [[UE-PakFile-源码解析：Pak 加载与 VFS]]

## 索引状态

- **所属 UE 阶段**：第三阶段 3.1 UObject 与组件/场景系统
- **对应 UE 笔记**：UE-CoreUObject-源码解析：Package 与加载
- **本轮分析完成度**：✅ 第一/二/三轮（骨架、血肉、关联）
- **分析日期**：2026-04-17

---
title: UE-构建系统-源码解析：模块依赖与 Build.cs
date: 2026-04-17
tags:
  - ue-source
  - engine-architecture
aliases:
  - UE Build.cs 模块依赖
  - UE ModuleRules
---

> [← 返回 UE全解析主索引]([[00-UE全解析主索引|UE全解析主索引]])

# UE-构建系统-源码解析：模块依赖与 Build.cs

## 模块定位

- **UE 模块路径**：`Engine/Source/Programs/UnrealBuildTool/Configuration/`
- **核心文件**：
  - `ModuleRules.cs` — `.Build.cs` 的基类，定义所有可配置属性
  - `UEBuildModule.cs` / `UEBuildModuleCPP.cs` — UBT 内部对模块的内存表示与编译环境组装
- **用户可见入口**：每个模块根目录下的 `<ModuleName>.Build.cs`
- **典型示例**：`Engine/Source/Runtime/Core/Core.Build.cs`

> UE 的模块系统是构建系统的核心组织单元。每个模块通过 `.Build.cs` 声明自己的依赖边界、包含路径、宏定义、PCH 策略等，UBT 在构建时将这些声明转化为编译器的 `-I`、`-D`、`-l` 参数。

---

## 接口梳理（第 1 层）

### .Build.cs 文件的本质

`.Build.cs` 是一个 C# 脚本文件，继承自 `ModuleRules` 基类。UBT 在启动时通过 **Roslyn 编译** 所有 `.Build.cs` 文件到一个临时的 Rules Assembly 中，然后通过反射实例化每个模块的 `ModuleRules`。

> 文件：`Engine/Source/Programs/UnrealBuildTool/Configuration/ModuleRules.cs`，第 103~200 行

```csharp
public partial class ModuleRules
{
    public enum ModuleType { CPlusPlus, External }
    public enum PCHUsageMode { Default, NoPCHs, NoSharedPCHs, UseSharedPCHs, UseExplicitOrSharedPCHs }
    public enum CodeOptimization { Never, InNonDebugBuilds, InShippingBuildsOnly, Always, Default }
    
    public string Name { get; internal set; }
    public ReadOnlyTargetRules Target { get; init; }
    ...
}
```

### ModuleRules 核心字段一览

| 字段 | 类型 | 作用 |
|------|------|------|
| `PublicDependencyModuleNames` | `List<string>` | 公共依赖模块。本模块 Public 头文件暴露的依赖，会**传递**给上层模块 |
| `PrivateDependencyModuleNames` | `List<string>` | 私有依赖模块。仅本模块实现使用，**不传递** |
| `PublicIncludePathModuleNames` | `List<string>` | 只需要头文件包含路径、不需要链接的公共模块 |
| `PrivateIncludePathModuleNames` | `List<string>` | 只需要头文件包含路径、不需要链接的私有模块 |
| `DynamicallyLoadedModuleNames` | `List<string>` | 运行时动态加载的模块（如插件）|
| `PublicDefinitions` / `PrivateDefinitions` | `List<string>` | 编译宏定义（`-D`）|
| `PublicIncludePaths` / `PrivateIncludePaths` | `List<string>` | 额外的包含目录（`-I`）|
| `PublicSystemIncludePaths` | `List<string>` | 系统包含目录（`-isystem`）|
| `PCHUsageMode` | `PCHUsageMode` | PCH 使用策略 |
| `PrivatePCHHeaderFile` / `SharedPCHHeaderFile` | `string` | 私有/共享 PCH 头文件 |
| `PublicAdditionalLibraries` | `List<string>` | 额外的静态/动态库 |
| `PublicFrameworks` | `List<string>` | iOS/macOS 框架 |
| `RuntimeDependencies` | `RuntimeDependencyList` | 运行时需要拷贝的文件 |
| `bLegacyPublicIncludePaths` | `bool` | 是否启用旧版 Public 子目录自动包含 |
| `bTreatAsEngineModule` | `bool` | 是否按引擎模块对待（影响 include order、废弃警告等）|

### 典型 Build.cs 示例：Core

> 文件：`Engine/Source/Runtime/Core/Core.Build.cs`

```csharp
public class Core : ModuleRules
{
    public Core(ReadOnlyTargetRules Target) : base(Target)
    {
        NumIncludedBytesPerUnityCPPOverride = 491520;
        PrivatePCHHeaderFile = "Private/CorePrivatePCH.h";
        SharedPCHHeaderFile = "Public/CoreSharedPCH.h";

        // 私有依赖：仅 Core 内部实现使用
        PrivateDependencyModuleNames.Add("BuildSettings");
        PrivateDependencyModuleNames.Add("AtomicQueue");
        PrivateDependencyModuleNames.Add("BLAKE3");

        // 公共依赖：使用 Core 的模块也会获得这些依赖的 include path
        PublicDependencyModuleNames.Add("GuidelinesSupportLibrary");
        PublicDependencyModuleNames.Add("TraceLog");

        // 仅需要 include path，不需要链接
        PrivateIncludePathModuleNames.AddRange(new string[] {
            "DerivedDataCache", "TargetPlatform", "Json", "RSA"
        });

        // 平台条件依赖
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PrivateDefinitions.Add("PLATFORM_BUILDS_LIBPAS=1");
            PrivateDependencyModuleNames.Add("libpas");
            AddEngineThirdPartyPrivateStaticDependencies(Target, "IntelTBB", "zlib");
        }
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            PublicFrameworks.AddRange(new string[] { "Cocoa", "IOKit", "Security" });
        }

        // 运行时依赖
        if (Target.WindowsPlatform.bUseBundledDbgHelp)
        {
            RuntimeDependencies.Add("$(EngineDir)/Binaries/ThirdParty/DbgHelp/.../dbghelp.dll");
        }
    }
}
```

`Core.Build.cs` 几乎用到了 `ModuleRules` 的所有能力：公共/私有依赖、include-only 模块、平台条件分支、第三方库、运行时依赖、PCH 配置。

---

## 数据结构（第 2 层）

### UEBuildModule：UBT 内部的模块对象

> 文件：`Engine/Source/Programs/UnrealBuildTool/Configuration/UEBuildModule.cs`，第 21~170 行

UBT 读取 `.Build.cs` 后，会创建对应的 `UEBuildModule` 对象（C++ 模块对应 `UEBuildModuleCPP`，外部模块对应 `UEBuildModuleExternal`）。

```csharp
abstract class UEBuildModule
{
    public readonly ModuleRules Rules;
    public readonly HashSet<DirectoryReference> PublicIncludePaths;
    public readonly HashSet<DirectoryReference> InternalIncludePaths;
    public readonly HashSet<DirectoryReference> PrivateIncludePaths;
    public readonly HashSet<DirectoryReference> PublicSystemIncludePaths;
    public readonly HashSet<string> PublicDefinitions;
    
    public List<UEBuildModule>? PublicDependencyModules;
    public List<UEBuildModule>? PrivateDependencyModules;
    public List<UEBuildModule>? PublicIncludePathModules;
    public List<UEBuildModule>? PrivateIncludePathModules;
    public List<UEBuildModule>? DynamicallyLoadedModules;
}
```

`UEBuildModule` 的职责是把 `ModuleRules` 中的**字符串列表**解析为**实际的模块对象引用**，并组装编译环境。

### Include Path 的自动发现

> 文件：`Engine/Source/Programs/UnrealBuildTool/Configuration/UEBuildModuleCPP.cs`，第 404~456 行

对于 C++ 模块，UBT 会自动扫描模块目录并填充标准包含路径：

```csharp
// 1. 模块根目录加入 LegacyPublicIncludePaths
LegacyPublicIncludePaths.Add(ModuleDir);

// 2. 如果存在 Classes/ 目录，加入 PublicIncludePaths
if (DirectoryLookupCache.DirectoryExistsAndContainsFiles(ClassesDirectory, SearchOption.AllDirectories))
{
    PublicIncludePaths.Add(ClassesDirectory);
}

// 3. 如果存在 Public/ 目录，加入 PublicIncludePaths，并递归扫描子目录加入 LegacyPublicIncludePaths
if (DirectoryLookupCache.DirectoryExistsAndContainsFiles(PublicDirectory, SearchOption.AllDirectories))
{
    PublicIncludePaths.Add(PublicDirectory);
    EnumerateLegacyIncludePaths(DirectoryItem.GetItemByDirectoryReference(PublicDirectory), 
        ExcludeNames, LegacyPublicIncludePaths);
}
```

这意味着：
- `Public/` 下的头文件默认对外可见
- `Public/` 下的子目录在旧版（`bLegacyPublicIncludePaths = true`）中也会被自动加入包含路径
- `Classes/` 目录（UHT 旧约定）也会被自动暴露
- `Private/` 目录**不会**被自动加入 `PublicIncludePaths`

---

## 行为分析（第 3 层）

### Include Path 传播机制

这是 UE 模块系统最核心的设计之一：**Public 依赖的 include path 会传递，Private 依赖的不会。**

#### AddModuleToCompileEnvironment

> 文件：`Engine/Source/Programs/UnrealBuildTool/Configuration/UEBuildModule.cs`，第 665~725 行

```csharp
public virtual void AddModuleToCompileEnvironment(
    UEBuildModule? SourceModule,
    UEBuildBinary? SourceBinary,
    HashSet<DirectoryReference> IncludePaths,
    HashSet<DirectoryReference> SystemIncludePaths,
    HashSet<DirectoryReference> ModuleInterfacePaths,
    List<string> Definitions,
    List<UEBuildFramework> AdditionalFrameworks,
    HashSet<FileItem> AutoRTFMExternalMappingFiles,
    List<FileItem> AdditionalPrerequisites,
    bool bLegacyPublicIncludePaths,
    bool bLegacyParentIncludePaths)
{
    // 1. 将自己的 PublicIncludePaths 加入调用者的编译环境
    AddIncludePaths(IncludePaths, PublicIncludePaths);

    // 2. 旧版兼容：LegacyParentIncludePaths 和模块父目录
    if (bLegacyParentIncludePaths)
    {
        AddIncludePaths(IncludePaths, LegacyParentIncludePaths);
        IncludePaths.Add(ModuleDirectory.ParentDirectory!);
    }
    if (bLegacyPublicIncludePaths)
    {
        AddIncludePaths(IncludePaths, LegacyPublicIncludePaths);
    }

    // 3. InternalIncludePaths 只在同 Scope 或 SourceModule 是引擎模块时共享
    if (SourceModule != null && (Rules.Context.Scope.Contains(SourceModule.Rules.Context.Scope) 
        || SourceModule.Rules.bTreatAsEngineModule))
    {
        AddIncludePaths(IncludePaths, InternalIncludePaths);
    }

    // 4. System include paths 和 Public Definitions
    SystemIncludePaths.UnionWith(PublicSystemIncludePaths);
    AddDefinitions(Definitions, PublicDefinitions);

    // 5. 生成 MODULE_API 宏（DLLEXPORT / DLLIMPORT）
    if (Rules.Type == ModuleRules.ModuleType.CPlusPlus)
    {
        string ApiDefinition = String.Empty;
        if (Rules.Target.LinkType == TargetLinkType.Monolithic)
        {
            if (Rules.Target.bShouldCompileAsDLL && ...)
                ApiDefinition = "DLLEXPORT";
        }
        else if (Binary == null || ...)
        {
            ApiDefinition = "DLLIMPORT";
        }
        else if (!(!Binary.bAllowExports || Rules.Target.bMergeModules))
        {
            ApiDefinition = "DLLEXPORT";
        }
        Definitions.Add(ModuleApiDefine + "=" + ApiDefinition);
    }
}
```

#### SetupPrivateCompileEnvironment

> 文件：`Engine/Source/Programs/UnrealBuildTool/Configuration/UEBuildModule.cs`，第 768~792 行

```csharp
protected virtual void SetupPrivateCompileEnvironment(...)
{
    // 1. 加入本模块的私有包含路径
    IncludePaths.UnionWith(PrivateIncludePaths);

    // 2. 查找所有需要参与编译环境的模块（递归遍历 Public/Private 依赖）
    Dictionary<UEBuildModule, bool> ModuleToIncludePathsOnlyFlag = new();
    FindModulesInPrivateCompileEnvironment(ModuleToIncludePathsOnlyFlag);

    // 3. 按发现顺序调用每个依赖模块的 AddModuleToCompileEnvironment
    foreach (UEBuildModule Module in ModuleToIncludePathsOnlyFlag.Keys)
    {
        Module.AddModuleToCompileEnvironment(this, Binary, IncludePaths, ...);
    }
}
```

`FindModulesInPrivateCompileEnvironment` 会递归遍历：
- 本模块的 `PublicDependencyModules` 和 `PrivateDependencyModules`
- 这些模块的 `PublicDependencyModules`
- `PublicIncludePathModules` 和 `PrivateIncludePathModules`

但**不会**继续深入 Private 依赖的 Private 依赖（只传递 Public）。

#### 传递规则总结

```
模块 A
├── PublicDependency: B  → B 的 PublicIncludePaths 传给 A，并继续传给 A 的依赖者
├── PrivateDependency: C → C 的 PublicIncludePaths 传给 A，但不传给 A 的依赖者
├── PublicIncludePath: D → D 的 PublicIncludePaths 传给 A，并继续传给 A 的依赖者（但不链接）
└── PrivateIncludePath: E → E 的 PublicIncludePaths 传给 A，不继续传递（不链接）
```

### 循环依赖白名单

> 文件：`Engine/Source/Programs/UnrealBuildTool/Configuration/UEBuildModuleCPP.cs`，第 180~295 行

UE 中存在大量历史遗留的模块循环依赖。UBT 没有强行禁止所有循环依赖，而是维护了一个**硬编码白名单**：

```csharp
static readonly KeyValuePair<string, string>[] CircularDependenciesAllowList =
[
    new("AIModule", "AITestSuite"),
    new("AnimGraph", "BlueprintGraph"),
    new("Engine", "AudioMixer"),
    new("Engine", "UnrealEd"),
    new("UnrealEd", "Kismet"),
    new("UnrealEd", "LevelEditor"),
    ... // 近百个条目
];
```

如果检测到不在白名单中的循环依赖，UBT 会报错。

### GetCompileEnvironment：组装完整编译环境

> 文件：`Engine/Source/Programs/UnrealBuildTool/Configuration/UEBuildModuleCPP.cs`，第 2680~2724 行

```csharp
public CppCompileEnvironment CreatePrivateCompileEnvironment(...)
{
    CppCompileEnvironment Result = new CppCompileEnvironment(BaseCompileEnvironment);
    
    // 1. 设置本模块的编译环境（Private paths + 依赖模块的 Public paths）
    SetupPrivateCompileEnvironment(Result.UserIncludePaths, Result.SystemIncludePaths, ...);

    // 2. 处理 ForceIncludeFiles（如 PCH）
    foreach (string ForceIncludeFile in Rules.ForceIncludeFiles)
    { ... }

    // 3. 继承依赖模块的 ExtraRootPath
    if (PrivateDependencyModules != null)
    {
        foreach (UEBuildModule Dependent in PrivateDependencyModules)
        {
            Result.RootPaths.AddExtraPath(Dependent.Rules.ExtraRootPath);
        }
    }

    return Result;
}
```

---

## 与上下层的关系

### 上层：TargetRules 与 UBT 构建流程

- `TargetRules`（`.Target.cs`）决定最终产出类型（Game、Editor、Server、Client、Program）
- `UEBuildTarget` 读取 `TargetRules` 后，递归收集所有依赖的 `ModuleRules`，实例化为 `UEBuildModule`
- `UEBuildBinary` 决定模块是链接进主 EXE/DLL、还是单独的 DLL

### 同层：UHT 与 Build.cs 的交互

- `.Build.cs` 中的 `PublicIncludePaths` / `PrivateIncludePaths` 直接影响 UHT 解析头文件时的包含路径
- UBT 在调用 UHT 前，会将模块的 include paths 写入 `.uhtmanifest`
- `bHasUObjects` 由 UHT 的解析结果决定，影响模块是否需要生成 `.generated.cpp`

### 下层：编译器与链接器

UBT 最终把 `UEBuildModule` 中的数据转化为：

| UEBuildModule 数据 | 编译器参数 |
|-------------------|-----------|
| `PublicIncludePaths` / `PrivateIncludePaths` | `-I<path>` |
| `PublicSystemIncludePaths` | `-isystem <path>` (GCC/Clang) 或 `/I <path>` (MSVC) |
| `PublicDefinitions` / `PrivateDefinitions` | `-D<MACRO>` 或 `/D <MACRO>` |
| `PublicAdditionalLibraries` | `-l<lib>` 或 `.lib` 文件路径 |
| `<MODULE>_API` | `DLLEXPORT` / `DLLIMPORT` |
| `ForceIncludeFiles` | `-include <file>` 或 `/FI <file>` |

---

## 设计亮点与可迁移经验

### 1. Public / Private 依赖边界清晰化

`PublicDependencyModuleNames` 与 `PrivateDependencyModuleNames` 的区分，本质上是在**构建图**层面强制执行 API 封装：

- 如果模块 A 的 Public 头文件 `#include` 了模块 B 的头文件，那么 B 必须是 A 的 `PublicDependency`
- 如果只有 `.cpp` 文件使用 B，那么 B 应该是 `PrivateDependency`

**可迁移经验**：在大型 C++ 项目中，显式区分 transitive 和 non-transitive 依赖，可以有效控制编译时间的指数级增长和 API 泄漏。

### 2. Include Path 的自动发现降低维护成本

UE 通过约定 `Public/`、`Private/`、`Classes/` 目录结构，自动推导大部分 include path，开发者通常不需要手动写 `PublicIncludePaths.Add(...)`。

**可迁移经验**：通过目录约定（convention over configuration）来减少构建配置的维护负担。

### 3. 条件依赖与平台抽象

`Core.Build.cs` 中大量使用了 `if (Target.Platform == ...)` 的条件分支。同一套 `ModuleRules` 类可以承载所有平台的差异化配置，而不需要为每个平台写独立的 `.Build.cs`。

**可迁移经验**：将平台差异集中在模块规则中，而不是散落在 CMake/Makefile 中，可以提高可维护性。

### 4. 编译期 API 宏自动生成

`CORE_API`、`ENGINE_API` 等宏不是手写维护的，而是由 UBT 根据模块的链接类型（Monolithic / Modular）和目标二进制自动注入为 `DLLEXPORT` 或 `DLLIMPORT`。

**可迁移经验**：对于跨 DLL 的 C++ 项目，让构建系统自动生成平台无关的导出宏，比手动维护 `_declspec(dllexport)` 更可靠。

### 5. 循环依赖白名单是历史债的折中方案

UBT 明知循环依赖有害，但出于兼容性考虑保留了白名单机制。这体现了大型项目演进中的**渐进式治理**策略：先阻止新增循环依赖，再逐步清理旧的。

---

## 关联阅读

- 前序笔记：[[UE-构建系统-源码解析：UBT 构建体系总览]]
- 前序笔记：[[UE-构建系统-源码解析：UHT 反射代码生成]]
- 后续笔记（待产出）：[[UE-构建系统-源码解析：UAT 自动化部署]]
- 相关专题：[[UE-专题：构建到部署的完整流水线]]

---

## 索引状态

- **所属 UE 阶段**：第一阶段 - 构建系统
- **对应 UE 笔记**：UE-构建系统-源码解析：模块依赖与 Build.cs
- **本轮完成度**：✅ 第三轮（已完成骨架扫描、血肉填充、关联辐射）
- **更新日期**：2026-04-17

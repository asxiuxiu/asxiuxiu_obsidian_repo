---
title: UE-SkeletalMeshEditor-源码解析：骨骼网格编辑器
date: 2026-04-19
tags: [ue-source, engine-architecture, skeletal-mesh]
aliases: [SkeletalMeshEditor 模块分析]
---

> [← 返回 UE全解析主索引]([[00-UE全解析主索引|UE全解析主索引]])

## Why：为什么要分析 SkeletalMeshEditor？

骨骼网格（Skeletal Mesh）是角色动画系统的核心资产。`SkeletalMeshEditor` 作为 UE 编辑器层中负责骨骼网格可视化和编辑的专用模块，承上启下：

- **向上**：通过 `Persona` 统一框架提供预览视口、骨骼树、Morph Target、Clothing 等可视化编辑能力。
- **向下**：直接操作 `USkeletalMesh` 的运行时数据结构（LOD、Section、Skin Weight、PhysicsAsset 关联），并触发渲染数据重建。
- **横向**：与 `SkeletonEditor`、`AnimationEditor`、`PhysicsAssetEditor` 共享 Persona 基础设施，是理解 UE 编辑器“多 Toolkit 协同”架构的典型样本。

## What：模块接口层

### 1. 模块依赖（Build.cs）

文件：`Engine/Source/Editor/SkeletalMeshEditor/SkeletalMeshEditor.Build.cs`（L1-L48）

```csharp
public class SkeletalMeshEditor : ModuleRules
{
    PublicDependencyModuleNames.AddRange(new string[] { "Persona" });
    PrivateDependencyModuleNames.AddRange(new string[] {
        "Core", "CoreUObject", "Engine", "Slate", "UnrealEd",
        "SkeletonEditor", "Kismet", "PropertyEditor",
        "ClothingSystemRuntimeCommon", "ClothingSystemEditorInterface",
        "PhysicsUtilities", "InterchangeCore", "InterchangeEngine",
        "EditorSubsystem", "ToolMenus", "StatusBar", ...
    });
}
```

**关键依赖解读**：

| 依赖模块 | 作用 |
|---------|------|
| `Persona`（Public） | 唯一的 Public 依赖，Persona 是动画/骨骼相关编辑器的统一宿主框架，提供预览场景、视口、Details 面板等通用基础设施。 |
| `SkeletonEditor` | 创建骨骼树（Skeleton Tree）面板。 |
| `ClothingSystemEditorInterface` / `ClothingSystemRuntimeCommon` | 布料数据（Clothing Asset）的创建、绑定与解绑。 |
| `PhysicsUtilities` | PhysicsAsset 的创建与兼容性校验。 |
| `InterchangeEngine` / `InterchangeCore` | 基于 Interchange 的异步重导入管线。 |
| `EditorSubsystem` | 支撑 `USkeletalMeshEditorSubsystem` 的生命周期。 |

### 2. 核心接口类

#### ISkeletalMeshEditorModule

文件：`Engine/Source/Editor/SkeletalMeshEditor/Public/ISkeletalMeshEditorModule.h`（L14-L31）

```cpp
class ISkeletalMeshEditorModule : public IModuleInterface,
                                   public IHasMenuExtensibility,
                                   public IHasToolBarExtensibility
{
public:
    virtual TSharedRef<ISkeletalMeshEditor> CreateSkeletalMeshEditor(...) = 0;
    virtual TArray<FSkeletalMeshEditorToolbarExtender>& GetAllSkeletalMeshEditorToolbarExtenders() = 0;
    virtual TArray<FOnSkeletalMeshEditorInitialized>& GetPostEditorInitDelegates() = 0;
    virtual FOnRegisterLayoutExtensions& OnRegisterLayoutExtensions() = 0;
};
```

- 继承 `IHasMenuExtensibility` / `IHasToolBarExtensibility`：支持外部模块通过 `FExtensibilityManager` 注入菜单和工具栏。
- `CreateSkeletalMeshEditor`：工厂方法，创建编辑器实例。
- `FOnRegisterLayoutExtensions`：允许 `USkeletalMeshEditorUISubsystem` 注册自定义布局扩展。

#### ISkeletalMeshEditor

文件：`Engine/Source/Editor/SkeletalMeshEditor/Public/ISkeletalMeshEditor.h`（L11-L20）

```cpp
class ISkeletalMeshEditor : public FPersonaAssetEditorToolkit, public IHasPersonaToolkit
{
public:
    virtual TSharedPtr<ISkeletalMeshEditorBinding> GetBinding() = 0;
    virtual FSimpleMulticastDelegate& OnPreSaveAsset() = 0;
    virtual TSharedPtr<FUICommandInfo> GetResetBoneTransformsCommand() = 0;
};
```

- 继承链：`FAssetEditorToolkit → FPersonaAssetEditorToolkit → ISkeletalMeshEditor`。
- `ISkeletalMeshEditorBinding`：用于向外部（如 Control Rig 模式）暴露骨骼选择通知能力。

#### USkeletalMeshEditorSubsystem

文件：`Engine/Source/Editor/SkeletalMeshEditor/Public/SkeletalMeshEditorSubsystem.h`（L38-L363）

```cpp
UCLASS(MinimalAPI)
class USkeletalMeshEditorSubsystem : public UEditorSubsystem
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category = "Skeletal Mesh Utilities")
    static bool RegenerateLOD(USkeletalMesh* SkeletalMesh, ...);

    UFUNCTION(BlueprintCallable, Category = "Editor Scripting | SkeletalMesh")
    static UPhysicsAsset* CreatePhysicsAsset(USkeletalMesh* SkeletalMesh, ...);

    UFUNCTION(BlueprintCallable, Category = "Editor Scripting | SkeletalMesh")
    static bool AssignPhysicsAsset(USkeletalMesh* TargetMesh, UPhysicsAsset* PhysicsAsset);
    ...
};
```

- 继承 `UEditorSubsystem`，随编辑器启动自动实例化，通过 `USubsystemBlueprintLibrary` 对蓝图/脚本暴露 API。
- 宏统计：`UCLASS` ×1、`UENUM` ×1、`UFUNCTION` ×30+，是典型的 Editor Scripting 门面类。

### 3. 编辑器核心实现类

#### FSkeletalMeshEditor

文件：`Engine/Source/Editor/SkeletalMeshEditor/Private/SkeletalMeshEditor.h`（L54-L252）

```cpp
class FSkeletalMeshEditor : public ISkeletalMeshEditor,
                             public FGCObject,
                             public FEditorUndoClient,
                             public FTickableEditorObject
{
    ...
    virtual void Tick(float DeltaTime) override;
    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual void PostUndo(bool bSuccess) override;
    ...
};
```

- 多重继承特征：
  - `FGCObject`：手动管理 `TObjectPtr<USkeletalMesh>` 的 GC 引用（`AddReferencedObjects`）。
  - `FEditorUndoClient`：接收全局 Undo/Redo 事件。
  - `FTickableEditorObject`：每帧驱动预览场景视图刷新。

## How：数据层（Structure）

### 1. 核心 UObject 派生类与生命周期

| 类名 | 基类 | Outer/Package | 生命周期管理者 |
|------|------|---------------|-------------|
| `USkeletalMeshEditorSubsystem` | `UEditorSubsystem` | 由 `UEngine` / `SubsystemCollection` 持有 | 编辑器进程级单例，随编辑器启动/关闭 |
| `USkeletalMeshEditorContextMenuContext` | `UObject` | 临时 NewObject，无持久 Outer | 栈局部，随菜单关闭由 GC 回收 |
| `USkeletalMeshEditorUISubsystem` | `UAssetEditorUISubsystem` | 同上 Subsystem 管理 | 编辑器进程级单例 |

### 2. FSkeletalMeshEditor 成员变量分析

文件：`Engine/Source/Editor/SkeletalMeshEditor/Private/SkeletalMeshEditor.h`（L212-L251）

```cpp
private:
    /** The skeleton we are editing */
    TObjectPtr<USkeletalMesh> SkeletalMesh;

    TSharedPtr<class IPersonaToolkit> PersonaToolkit;
    TSharedPtr<class ISkeletonTree> SkeletonTree;
    TSharedPtr<class IMorphTargetViewer> MorphTargetViewer;
    TSharedPtr<class IPersonaViewport> Viewport;
    TSharedPtr<class IDetailsView> DetailsView;

    TSharedPtr<IToolkit> HostedToolkit;
    TSharedPtr<FSkeletalMeshEditorModeUILayer> ModeUILayer;
    TSharedPtr<SSkeletalMeshEditorToolbox> ToolboxWidget;
    TSharedPtr<FSkeletalMeshEditorBinding> Binding;

    FSimpleMulticastDelegate OnPreSaveAssetDelegate;
    FSimpleMulticastDelegate OnPreSaveAssetAsDelegate;
```

**内存与引用语义分析**：

| 成员 | 类型 | 语义 | 内存来源 |
|------|------|------|---------|
| `SkeletalMesh` | `TObjectPtr<USkeletalMesh>` | 强引用 UObject，受 `FGCObject::AddReferencedObjects` 保护 | 传入的编辑对象，Package 内分配 |
| `PersonaToolkit` | `TSharedPtr<IPersonaToolkit>` | 共享指针，Persona 框架的核心句柄 | `FPersonaModule::CreatePersonaToolkit`（堆上 `MakeShareable`） |
| `SkeletonTree` | `TSharedPtr<ISkeletonTree>` | 骨骼树面板接口 | `ISkeletonEditorModule::CreateSkeletonTree` |
| `Viewport` | `TSharedPtr<IPersonaViewport>` | 预览视口 | `PersonaModule.RegisterPersonaViewportTabFactories` |
| `HostedToolkit` | `TSharedPtr<IToolkit>` | 当前托管的子 Toolkit（如 ClothPaint 模式） | 外部注入 |
| `Binding` | `TSharedPtr<FSkeletalMeshEditorBinding>` | 编辑器绑定器，对外暴露通知接口 | 懒创建（`GetBinding` 时 `MakeShared`） |

**关键**：`FSkeletalMeshEditor` 本身不是 UObject，而是通过 `TSharedRef/Ptr` 在 Slate/Toolkit 体系中流转；它通过 `FGCObject` 契约确保 `SkeletalMesh` 不会在编辑期间被 GC。

### 3. FSkeletalMeshEditorBinding / Notifier 委托链

文件：`Engine/Source/Editor/SkeletalMeshEditor/Private/SkeletalMeshEditor.h`（L257-L284）

```cpp
class FSkeletalMeshEditorNotifier: public ISkeletalMeshNotifier
{
    TWeakPtr<FSkeletalMeshEditor> Editor;
};

class FSkeletalMeshEditorBinding: public ISkeletalMeshEditorBinding
{
    TWeakPtr<FSkeletalMeshEditor> Editor;
    TSharedPtr<FSkeletalMeshEditorNotifier> Notifier;
};
```

- `TWeakPtr` 的使用避免了 `Binding/Notifier` 与 `FSkeletalMeshEditor` 之间的循环引用（因为编辑器本身已被 `ToolkitManager` 持有）。
- 当编辑器关闭时，`WeakPtr` 自然失效，通知链安全断开。

### 4. USkeletalMeshEditorSubsystem 的脚本门面

文件：`Engine/Source/Editor/SkeletalMeshEditor/Public/SkeletalMeshEditorSubsystem.h`

- 所有 `UFUNCTION` 均为 **静态或实例方法**，通过 `EditorScriptingHelpers::CheckIfInEditorAndPIE()` 进行环境校验。
- 修改资产前统一调用 `SkeletalMesh->Modify()` + `FScopedTransaction`，确保支持 Undo。
- 修改后统一调用 `SkeletalMesh->PostEditChange()`，触发渲染数据重建。

## How：逻辑层（Behavior）

### 核心流程 1：编辑器初始化与骨骼预览

文件：`Engine/Source/Editor/SkeletalMeshEditor/Private/SkeletalMeshEditor.cpp`（L243-L301）

```cpp
void FSkeletalMeshEditor::InitSkeletalMeshEditor(..., USkeletalMesh* InSkeletalMesh)
{
    SkeletalMesh = InSkeletalMesh;

    // 1. 创建 PersonaToolkit，初始化预览场景
    FPersonaModule& PersonaModule = FModuleManager::LoadModuleChecked<FPersonaModule>("Persona");
    PersonaToolkit = PersonaModule.CreatePersonaToolkit(InSkeletalMesh, PersonaToolkitArgs);
    PersonaToolkit->GetPreviewScene()->SetDefaultAnimationMode(EPreviewSceneDefaultAnimationMode::ReferencePose);

    // 2. 创建 SkeletonTree（依赖 SkeletonEditor 模块）
    ISkeletonEditorModule& SkeletonEditorModule = ...;
    SkeletonTree = SkeletonEditorModule.CreateSkeletonTree(PersonaToolkit->GetSkeleton(), SkeletonTreeArgs);

    // 3. 创建 MorphTarget 查看器
    MorphTargetViewer = PersonaModule.CreateDefaultMorphTargetViewerWidget(...);

    // 4. 初始化 AssetEditorToolkit 框架
    FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, ...);

    // 5. 注册 ApplicationMode（含 TabLayout）
    AddApplicationMode(SkeletalMeshEditorModes::SkeletalMeshEditorMode,
        MakeShareable(new FSkeletalMeshEditorMode(SharedThis(this), ...)));
    SetCurrentMode(SkeletalMeshEditorModes::SkeletalMeshEditorMode);

    // 6. 绑定命令、扩展菜单/工具栏
    BindCommands();
    ExtendMenu();
    ExtendToolbar();

    // 7. 注册网格点击选择（用于 Section/Clothing 编辑）
    PreviewScene->RegisterOnMeshClick(FOnMeshClick::CreateSP(this, &FSkeletalMeshEditor::HandleMeshClick));
    PreviewScene->SetAllowMeshHitProxies(true);
}
```

**调用链与交互点**：

```
内容浏览器双击资产
    → UAssetEditorSubsystem::OpenEditorForAsset
        → ISkeletalMeshEditorModule::CreateSkeletalMeshEditor
            → FSkeletalMeshEditor::InitSkeletalMeshEditor
                → FPersonaModule::CreatePersonaToolkit  [→ Runtime/Engine: USkeletalMesh, UDebugSkelMeshComponent]
                → ISkeletonEditorModule::CreateSkeletonTree  [→ 依赖 SkeletonEditor 模块]
                → FSkeletalMeshEditorMode 注册 TabFactories
                    → FPersonaModule::RegisterPersonaViewportTabFactories  [→ 视口渲染]
```

### 核心流程 2：LOD 重导入与链式重建

文件：`Engine/Source/Editor/SkeletalMeshEditor/Private/SkeletalMeshEditor.cpp`（L1363-L1535）

```cpp
TFuture<bool> FSkeletalMeshEditor::HandleReimportMeshInternal(const FReimportParameters& ReimportParameters)
{
    TSharedPtr<TPromise<bool>> Promise = MakeShared<TPromise<bool>>();

    // 通过 Interchange 进行异步重导入
    UE::Interchange::FAssetImportResultRef Result = FReimportManager::Instance()->ReimportAsync(
        SkeletalMesh, true, true, TEXT(""), nullptr,
        ReimportParameters.SourceFileIndex,
        ReimportParameters.bWithNewFile,
        false,  // bAutomated
        ReimportParameters.bReimportWithDialog
    );

    // 异步回调：完成后刷新 SkeletonTree 并设置 Promise
    Result->OnDone([Promise, SkeletonTreePtr, WeakSkeletalMesh](UE::Interchange::FImportResult& Result)
    {
        auto ResetComponent = [Promise, SkeletonTreePtr, WeakSkeletalMesh, &Result]()
        {
            SkeletonTreePtr->Refresh();  // 刷新骨骼树
            ...
            Promise->SetValue(true);
        };

        if (IsInGameThread())
        {
            ResetComponent();
        }
        else
        {
            Async(EAsyncExecution::TaskGraphMainThread, MoveTemp(ResetComponent));
        }
    });
    return Promise->GetFuture();
}
```

**`HandleReimportAllMeshInternal` 的链式 LOD 处理**：

文件：`Engine/Source/Editor/SkeletalMeshEditor/Private/SkeletalMeshEditor.cpp`（L1409-L1535）

```cpp
TFuture<bool> ReimportLodInChain(..., int32 LodIndex)
{
    ...
    if (!bImportWithBaseMesh && !bHasBeenSimplified)
    {
        // 自定义 LOD：弹出对话框重导入
        FbxMeshUtils::ImportMeshLODDialog(SkeletalMesh, LodIndex, false)
            .Then([...](TFuture<bool> FutureResult) {
                // 递归处理下一个 LOD
                ReimportLodInChain(..., LodIndex + 1).Then(...);
            });
    }
    else if (bHasBeenSimplified && Dependencies[BaseLOD])
    {
        // 简化生成的 LOD：基于上级 LOD 重新生成
        FLODUtilities::SimplifySkeletalMeshLOD(UpdateContext, LodIndex, ...);
    }
    ...
}
```

**多线程与性能分析**：

1. **Interchange 异步导入**：`ReimportAsync` 将文件 I/O 和解析放在工作线程，主线程通过 `TFuture/TPromise` 等待结果，避免编辑器卡顿。
2. **链式 LOD 递归**：使用 `TFuture::Then` 形成异步链，保证 LOD 0 → LOD 1 → LOD N 的顺序依赖（简化型 LOD 必须等基础 LOD 就绪）。
3. **关键同步点**：`FScopedSkeletalMeshReregisterContexts` 在重导入期间注销并重新注册所有 `USkeletalMeshComponent`，确保渲染线程不访问正在重建的渲染数据。

### 核心流程 3：PhysicsAsset 创建与关联

文件：`Engine/Source/Editor/SkeletalMeshEditor/Private/SkeletalMeshEditorSubsystem.cpp`（L937-L1073）

```cpp
UPhysicsAsset* USkeletalMeshEditorSubsystem::CreatePhysicsAsset(USkeletalMesh* SkeletalMesh, bool bSetToMesh, int32 LodIndex)
{
    // 1. 在 SkeletalMesh 同级 Package 下创建 PhysicsAsset
    FString PackageName = SkeletalMesh->GetOutermost()->GetName();
    FString ParentPath = FString::Printf(TEXT("%s/%s"), *FPackageName::GetLongPackagePath(*PackageName), *ObjectName);
    UObject* Package = CreatePackage(*ParentPath);
    UObject* Object = NewObject<UPhysicsAsset>(Package, *ObjectName, RF_Public | RF_Standalone);

    // 2. 调用 PhysicsUtilities 生成 BodySetup / Constraint
    FPhysAssetCreateParams NewBodyData;
    NewBodyData.LodIndex = LodIndex;
    bool bSuccess = FPhysicsAssetUtils::CreateFromSkeletalMesh(NewPhysicsAsset, SkeletalMesh, NewBodyData, ...);

    // 3. 刷新关联的 SkeletalMeshComponent
    RefreshSkelMeshOnPhysicsAssetChange(SkeletalMesh);
    return NewPhysicsAsset;
}
```

```cpp
bool USkeletalMeshEditorSubsystem::AssignPhysicsAsset(USkeletalMesh* TargetMesh, UPhysicsAsset* PhysicsAsset)
{
    // 兼容性校验：所有 BodySetup 的 BoneName 和 Constraint 的 Bone 必须存在于 RefSkeleton
    if (!IsPhysicsAssetCompatible(TargetMesh, PhysicsAsset))
        return false;

    {
        FScopedTransaction Transaction(LOCTEXT("SetSkeletalMeshPhysicsAsset", "..."));
        TargetMesh->Modify();
        TargetMesh->SetPhysicsAsset(PhysicsAsset);
    }

    RefreshSkelMeshOnPhysicsAssetChange(TargetMesh);
    return true;
}
```

**上下层交互点**：

- **与 Physics 模块交互**：通过 `FPhysicsAssetUtils::CreateFromSkeletalMesh`（`PhysicsUtilities` 模块）生成物理形体和约束。
- **与 Runtime/Engine 交互**：`TargetMesh->SetPhysicsAsset` 直接修改 `USkeletalMesh` 的运行时属性；`RefreshSkelMeshOnPhysicsAssetChange` 遍历所有 `USkeletalMeshComponent` 重新初始化物理状态（`InitPhysics`）。
- **Package 关系**：PhysicsAsset 与 SkeletalMesh 处于同级目录，便于资源管理和版本控制。

### 附加流程：Clothing（布料）绑定与解绑

文件：`Engine/Source/Editor/SkeletalMeshEditor/Private/SkeletalMeshEditor.cpp`（L995-L1086）

```cpp
void FSkeletalMeshEditor::ApplyClothing(UClothingAssetBase* InAsset, int32 InLodIndex, int32 InSectionIndex, int32 InClothingLod)
{
    FScopedSuspendAlternateSkinWeightPreview ScopedSuspendAlternateSkinnWeightPreview(Mesh);
    {
        FScopedSkeletalMeshPostEditChange ScopedPostEditChange(Mesh);
        FScopedTransaction Transaction(LOCTEXT("...", "Persona editor: Apply Section Cloth"));
        Mesh->Modify();

        // 解绑已有的 ClothingAsset
        if (UClothingAssetBase* CurrentAsset = Mesh->GetSectionClothingAsset(InLodIndex, InSectionIndex))
        {
            CurrentAsset->UnbindFromSkeletalMesh(Mesh, InLodIndex);
        }

        // 绑定新的 ClothingAsset
        if (InAsset->BindToSkeletalMesh(Mesh, InLodIndex, InSectionIndex, InClothingLod))
        {
            OriginalSectionData.CorrespondClothAssetIndex = static_cast<int16>(AssetIndex);
            OriginalSectionData.ClothingData.AssetGuid = InAsset->GetAssetGuid();
            OriginalSectionData.ClothingData.AssetLodIndex = InClothingLod;
        }
    }
}
```

- **Section 级粒度**：布料绑定精确到 `LODIndex + SectionIndex`，通过 `FSkelMeshSourceSectionUserData` 持久化关联关系。
- **Scoped 模式**：`FScopedSuspendAlternateSkinWeightPreview` 和 `FScopedSkeletalMeshPostEditChange` 确保在事务范围内暂停 Skin Weight 预览并延迟渲染重建，避免中间态渲染错误。

## 性能与线程安全要点

1. **Tick 频率控制**：
   文件：`Engine/Source/Editor/SkeletalMeshEditor/Private/SkeletalMeshEditor.cpp`（L1305-L1308）
   ```cpp
   void FSkeletalMeshEditor::Tick(float DeltaTime)
   {
       GetPersonaToolkit()->GetPreviewScene()->InvalidateViews();
   }
   ```
   `ETickableTickType::Always` 意味着每帧都刷新视口，对于复杂角色这是一个持续性的 GPU/CPU 开销，但在编辑器场景下可接受。

2. **渲染数据重建的同步等待**：
   `SkeletalMesh->PostEditChange()` 内部会触发 `FSkeletalMeshRenderData` 的重新构建。在编辑器中这通常以 **同步方式** 完成（阻塞主线程），因此大网格的 LOD 生成或 Clothing 绑定可能导致短暂卡顿。

3. **异步导入回调的线程安全**：
   `HandleReimportMeshInternal` 的 `OnDone` 回调可能在工作线程触发，通过 `Async(EAsyncExecution::TaskGraphMainThread, ...)` 将 UI 刷新（`SkeletonTree->Refresh()`）切回游戏线程执行，符合 Slate 的线程约束。

## 模块架构图

```
┌─────────────────────────────────────────────────────────────┐
│                      SkeletalMeshEditor                      │
│  (Editor 模块，三层剥离对象)                                   │
├─────────────────────────────────────────────────────────────┤
│  接口层                                                       │
│    ISkeletalMeshEditorModule ──→ 工厂 + Extensibility       │
│    ISkeletalMeshEditor       ──→ PersonaAssetEditorToolkit  │
│    USkeletalMeshEditorSubsystem ─→ EditorSubsystem (脚本门面) │
├─────────────────────────────────────────────────────────────┤
│  数据层                                                       │
│    FSkeletalMeshEditor                                       │
│      ├─ TObjectPtr<USkeletalMesh>  (FGCObject 保护)          │
│      ├─ TSharedPtr<IPersonaToolkit>                         │
│      ├─ TSharedPtr<ISkeletonTree>                           │
│      ├─ TSharedPtr<IPersonaViewport>                        │
│      └─ TSharedPtr<FSkeletalMeshEditorBinding>              │
├─────────────────────────────────────────────────────────────┤
│  逻辑层                                                       │
│    InitSkeletalMeshEditor() ──→ 组装 Persona / Tab / Mode   │
│    HandleReimportMesh()     ──→ Interchange 异步 + LOD 链    │
│    CreatePhysicsAsset()     ──→ PhysicsUtilities 生成物理数据│
│    ApplyClothing()          ──→ ClothingSystemEditorInterface│
└─────────────────────────────────────────────────────────────┘
                              ↕
┌─────────────────────────────────────────────────────────────┐
│                    上下层模块交互                              │
│    ↑ Persona (预览场景、视口、Details)                        │
│    ↑ SkeletonEditor (骨骼树)                                 │
│    ↓ Runtime/Engine (USkeletalMesh、渲染数据重建)             │
│    ↓ PhysicsUtilities (PhysicsAsset 创建)                    │
│    ↓ ClothingSystemRuntimeCommon (布料模拟数据)               │
│    ↓ InterchangeEngine (异步资源导入管线)                     │
└─────────────────────────────────────────────────────────────┘
```

## 可迁移到自研引擎的工程原理

1. **Toolkit 组合模式**：UE 通过 `FPersonaAssetEditorToolkit` 将“预览场景 + 视口 + Details + 树形面板”抽象为可复用的编辑器基座。自研引擎可借鉴这种“宿主框架（Persona）+ 专用编辑器插件”的架构，避免每个资产编辑器重复造轮子。
2. **异步脚本门面**：`UEditorSubsystem` + `UFUNCTION(BlueprintCallable)` 提供了类型安全、可自动发现、支持 Undo 的脚本 API。这比直接暴露原始 C++ 接口更稳健。
3. **异步链式操作**：LOD 重导入使用 `TFuture::Then` 构建依赖链，而非阻塞等待。这种“基于 Future 的异步工作流”在编辑器 I/O 密集型操作中非常实用。
4. **Scoped 事务守卫**：`FScopedTransaction` + `FScopedSkeletalMeshPostEditChange` 的组合，确保数据修改和副作用（渲染重建）以原子事务的形式呈现给用户，是编辑器级 Undo 系统的最佳实践。

---

## 索引状态

- **所属阶段**：第五阶段-编辑器层-5.2 可视化编辑工具
- **状态**：✅ 完成
- **笔记编号**：5.2.5
- **模块路径**：`Engine/Source/Editor/SkeletalMeshEditor`

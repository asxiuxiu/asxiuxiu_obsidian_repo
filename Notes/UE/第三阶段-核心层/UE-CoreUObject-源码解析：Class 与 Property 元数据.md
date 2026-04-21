---
title: UE-CoreUObject-源码解析：Class 与 Property 元数据
date: 2026-04-17
tags:
  - ue-source
  - engine-architecture
aliases:
  - UE CoreUObject Class Property 元数据
---

> [[00-UE全解析主索引|← 返回 UE全解析主索引]]

# UE-CoreUObject-源码解析：Class 与 Property 元数据

## Why：为什么要深入理解反射元数据？

UHT（Unreal Header Tool）生成的 `.generated.h` 只是编译期的"原料"。真正让这些宏发挥作用的是运行时的 `UClass`、`UStruct`、`FProperty` 体系。理解这套元数据系统，才能解释 UE 的序列化、网络复制、蓝图访问、GC 引用追踪等功能是如何实现的。

## What：Class 与 Property 元数据是什么？

UE 的反射元数据由两条继承链构成：
1. **UObject 侧类型链**：`UObject` → `UField` → `UStruct` → `UClass` / `UScriptStruct` / `UFunction`
2. **FField 侧属性链**（UE5 主推）：`FField` → `FProperty` → `FNumericProperty` / `FObjectPropertyBase` / `FStructProperty` 等

UE5 的关键演进：将属性体系从 `UProperty`（UObject，GC 管理）全面重构为 `FProperty`（FField，轻量级非 UObject），显著降低了反射系统的内存开销。

---

## 模块定位

- **UE 模块路径**：`Engine/Source/Runtime/CoreUObject/`
- **Build.cs 文件**：`CoreUObject.Build.cs`
- **关键目录**：
  - `Public/UObject/Class.h` — `UField`、`UStruct`、`UClass`
  - `Public/UObject/Field.h` — `FField`、`FFieldClass`
  - `Public/UObject/UnrealType.h` — `FProperty` 及所有子类
  - `Public/UObject/UnrealTypePrivate.h` — 旧版 `UProperty` 兼容层

---

## 接口梳理（第 1 层）

### 核心头文件一览

| 头文件 | 核心类 | 职责 |
|--------|--------|------|
| `Class.h` | `UField`、`UStruct`、`UClass` | 反射类型描述体系的核心 |
| `Field.h` | `FField`、`FFieldClass`、`FFieldVariant` | UE5 新属性基类体系 |
| `UnrealType.h` | `FProperty` 及所有子类 | 属性内存布局、序列化、访问 |
| `ObjectMacros.h` | `EClassFlags`、`EClassCastFlags` | 反射宏与标志位定义 |
| `UObjectGlobals.h` | `UECodeGen_Private::FPropertyParamsBase` | UHT 生成的编译期参数结构 |

### UObject 侧类型继承链

```
UObject
└── UField
    ├── UStruct
    │   ├── UClass
    │   ├── UScriptStruct
    │   └── UFunction
    └── UEnum
```

### FField 侧属性继承链（UE5）

```
FField
└── FProperty
    ├── FNumericProperty
    │   └── FByteProperty / FIntProperty / FFloatProperty / FDoubleProperty ...
    ├── FObjectPropertyBase
    │   ├── FObjectProperty
    │   ├── FWeakObjectProperty
    │   ├── FLazyObjectProperty
    │   ├── FSoftObjectProperty
    │   ├── FClassProperty
    │   └── FSoftClassProperty
    ├── FBoolProperty
    ├── FNameProperty
    ├── FStrProperty / FTextProperty
    ├── FStructProperty
    ├── FArrayProperty
    ├── FMapProperty
    ├── FSetProperty
    ├── FDelegateProperty
    ├── FMulticastInlineDelegateProperty / FMulticastSparseDelegateProperty
    └── ...
```

---

## 数据结构（第 2 层）

### UStruct 中的 Property 链接链

> 文件：`Engine/Source/Runtime/CoreUObject/Public/UObject/Class.h`，第 529~536 行附近

```cpp
class COREUOBJECT_API UStruct : public UField
{
    FProperty* PropertyLink;      // 所有属性链表（most-derived -> base）
    FProperty* RefLink;           // 对象引用属性链表（GC 用）
    FProperty* DestructorLink;    // 需要析构的属性链表
    FProperty* PostConstructLink; // 构造后需要初始化的属性链表
};
```

### FProperty 中的 Next 指针

> 文件：`Engine/Source/Runtime/CoreUObject/Public/UObject/UnrealType.h`，第 211~238 行附近

```cpp
class COREUOBJECT_API FProperty : public FField
{
    FProperty* PropertyLinkNext = nullptr;       // 链入 PropertyLink
    FProperty* NextRef = nullptr;                // 链入 RefLink
    FProperty* DestructorLinkNext = nullptr;     // 链入 DestructorLink
    FProperty* PostConstructLinkNext = nullptr;  // 链入 PostConstructLink
};
```

### UClass 核心字段

> 文件：`Engine/Source/Runtime/CoreUObject/Public/UObject/Class.h`，第 3792 行附近

```cpp
class COREUOBJECT_API UClass : public UStruct
{
    ClassConstructorType ClassConstructor;           // CDO 构造函数指针
    FUObjectCppClassStaticFunctions CppClassStaticFunctions;
    TObjectPtr<UObject> ClassDefaultObject;          // CDO（Class Default Object）
    EClassFlags ClassFlags;
    EClassCastFlags ClassCastFlags;
    TArray<FRepRecord> ClassReps;                    // 网络复制属性记录
    TMap<FName, TObjectPtr<UFunction>> FuncMap;      // 函数映射
};
```

### UHT 编译期参数结构

> 文件：`Engine/Source/Runtime/CoreUObject/Public/UObject/UObjectGlobals.h`

```cpp
namespace UECodeGen_Private {
    struct FPropertyParamsBaseWithOffset {
        const char*    NameUTF8;
        const char*    RepNotifyFuncUTF8;
        EPropertyFlags PropertyFlags;
        EPropertyGenFlags Flags;
        SetterFuncPtr  SetterFunc;
        GetterFuncPtr  GetterFunc;
        uint16         ArrayDim;
        int32          Offset;           // 内存偏移
    };
}
```

---

## 行为分析（第 3 层）

### 运行时注册流程

1. `ProcessNewlyLoadedUObjects()` 遍历编译期注册的 `UClass`/`UEnum`/`UScriptStruct`。
2. 对每个类型调用 `DeferredRegister()`，完成命名、Outer 设置、Flag 初始化。
3. `UStruct::Link()` 被调用，遍历 `ChildProperties` 链表：
   - 计算每个 `FProperty` 的 `Offset_Internal`
   - 按类型将属性分别挂入 `PropertyLink`、`RefLink`、`DestructorLink`、`PostConstructLink`
4. `UClass::GetDefaultObject()` 触发 `InternalCreateDefaultObjectWrapper()`，调用 `ClassConstructor` 创建 CDO。

### GetDefaultObject 实现

> 文件：`Engine/Source/Runtime/CoreUObject/Public/UObject/Class.h`，第 4373 行附近

```cpp
UObject* GetDefaultObject(bool bCreateIfNeeded = true) const
{
    if (ClassDefaultObject == nullptr && bCreateIfNeeded)
    {
        InternalCreateDefaultObjectWrapper();
    }
    return ClassDefaultObject;
}
```

### UE4 vs UE5 反射演进

| 维度 | UE4 | UE5 |
|------|-----|-----|
| Property 基类 | `UProperty` (UObject) | `FProperty` (FField) |
| 内存开销 | 每个 UProperty 都是 UObject，GC 压力大 | FProperty 脱离 UObject GC |
| 注册方式 | 运行时堆分配 | `UE_WITH_CONSTINIT_UOBJECT` 支持编译期 constinit |
| 兼容层 | 直接可用 | `UnrealTypePrivate.h` 保留旧 UProperty 类 |

---

## 上下层关系

### 上层调用者

| 模块 | 使用方式 |
|------|---------|
| `Engine` | 通过 `UClass` 创建 Actor，通过 `FProperty` 序列化组件属性 |
| `BlueprintGraph` | 解析 `UFunction`、`FProperty` 生成蓝图节点 |
| `Net` | 通过 `ClassReps` 和 `FProperty` 实现网络复制 |

### 下层依赖

| 模块 | 作用 |
|------|------|
| `UHT` | 编译期生成 `.generated.h` 和参数表 |
| `Core` | 提供 `FName`、`TMap`、`TArray` 等基础容器 |

---

## 设计亮点与可迁移经验

1. **类型与属性分离**：`UClass` 负责"是什么"，`FProperty` 负责"怎么存"。职责清晰，便于分别优化。
2. **多链表组织属性**：`PropertyLink`、`RefLink`、`DestructorLink`、`PostConstructLink` 将不同用途的属性分组，避免了遍历所有属性时的分支预测失败。
3. **CDO 作为模板**：每个 `UClass` 持有一个 `ClassDefaultObject`，作为该类的属性默认值模板。序列化时只需存"与 CDO 的差异"，极大压缩了包体积。
4. **编译期参数 + 运行时注册**：UHT 生成静态参数表，引擎启动时批量注册，兼顾了灵活性和启动性能。

---

## 关键源码片段

### UClass 核心字段

> 文件：`Engine/Source/Runtime/CoreUObject/Public/UObject/Class.h`，第 3792 行附近

```cpp
class COREUOBJECT_API UClass : public UStruct
{
    ClassConstructorType ClassConstructor;
    TObjectPtr<UObject> ClassDefaultObject;
    EClassFlags ClassFlags;
    EClassCastFlags ClassCastFlags;
    TArray<FRepRecord> ClassReps;
    TMap<FName, TObjectPtr<UFunction>> FuncMap;
};
```

### UStruct::Link 中的属性链表构建
（由 UHT 生成的代码在注册时触发，`Link` 会计算 Offset 并建立链表关系）

---

## 关联阅读

- [[UE-CoreUObject-源码解析：UObject 体系总览]]
- [[UE-CoreUObject-源码解析：反射系统与 UHT]]
- [[UE-CoreUObject-源码解析：GC 与对象生命周期]]
- [[UE-Engine-源码解析：Actor 与 Component 模型]]

## 索引状态

- **所属 UE 阶段**：第三阶段 3.1 UObject 与组件/场景系统
- **对应 UE 笔记**：UE-CoreUObject-源码解析：Class 与 Property 元数据
- **本轮分析完成度**：✅ 第一/二/三轮（骨架、血肉、关联）
- **分析日期**：2026-04-17

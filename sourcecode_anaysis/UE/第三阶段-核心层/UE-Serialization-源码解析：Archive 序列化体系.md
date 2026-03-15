---
title: UE-Serialization-源码解析：Archive 序列化体系
date: 2026-04-17
tags:
  - ue-source
  - engine-architecture
  - serialization
aliases:
  - UE-Serialization-Archive
---

> [[00-UE全解析主索引|← 返回 00-UE全解析主索引]]

## Why：为什么要理解 Archive 序列化体系？

Unreal Engine 中几乎所有数据持久化、网络传输、内存拷贝、资源加载都建立在 `FArchive` 之上。从 `.uasset` 包文件加载到 UObject 的 `Serialize()`，从配置文件读写至内存缓冲序列化，`FArchive` 及其派生类构成了 UE 的"数据搬运血管"。理解这套体系是解析资源加载、网络同步、Cook 打包等上层机制的必经之路。

## What：Archive 体系是什么？

`FArchive` 是 UE 序列化体系的核心抽象基类，采用**流式字节读写 + 运算符重载**的设计模式。它不直接依赖 UObject（Core 模块级别），但通过虚函数为 UObject、FName、FText 等类型预留了扩展点。围绕 `FArchive` 形成了三大派生家族：

1. **文件 Archive**：`FArchiveFileReaderGeneric`、`FArchiveFileWriterGeneric` — 直接映射磁盘文件
2. **内存 Archive**：`FMemoryArchive` → `FMemoryReader` / `FMemoryWriter` — 在 `TArray<uint8>` 上读写
3. **代理 Archive**：`FArchiveProxy` → `FNameAsStringProxyArchive` / `FObjectAndNameAsStringProxyArchive` — 包装并增强行为

### 核心类定位

| 类 | 模块 | 职责 |
|---|---|---|
| `FArchiveState` | Core | 状态机基类，维护 `ArIsLoading`、`ArIsSaving`、`ArIsError` 等标志 |
| `FArchive` | Core | 主接口，声明 `Serialize()`、`Tell()`、`Seek()`、`operator<<` 等 |
| `FMemoryArchive` | Core | 纯内存序列化基类，维护 `Offset` |
| `FMemoryReader` / `FMemoryWriter` | Core | 从 `TArray<uint8>` 读 / 向 `TArray<uint8>` 写 |
| `FBufferArchive` | Core | 自身继承 `TArray<uint8>`，同时是 Archive |
| `FObjectReader` / `FObjectWriter` | CoreUObject | 专门处理 UObject 的内存序列化 |
| `FObjectAndNameAsStringProxyArchive` | CoreUObject | 将 UObject 引用和 FName 序列化为字符串 |

## How：Archive 的三层源码剖析

### 第 1 层：接口层（What）

#### FArchive 核心接口

> 文件：`Engine/Source/Runtime/Core/Public/Serialization/Archive.h`，第 69~150 行

```cpp
struct FArchiveState
{
    virtual int64 Tell() { return INDEX_NONE; }
    virtual void Seek(int64 InPos) {}
    virtual void Serialize(void* V, int64 Length) {}
    // ... 状态标志：ArIsLoading、ArIsSaving、ArIsError 等
};

class FArchive : public FArchiveState
{
    virtual void Serialize(void* V, int64 Length);
    virtual int64 Tell();
    virtual void Seek(int64 InPos);
    virtual void Flush();
    
    virtual FArchive& operator<<(FName& Value);
    virtual FArchive& operator<<(FText& Value);
    virtual FArchive& operator<<(UObject*& Value);
    // ... 标量类型的 friend operator<<
};
```

`FArchive` 的设计亮点：
- **双向统一**：同一份 `Serialize()` / `operator<<` 代码，根据 `IsLoading()` 标志自动切换读写方向。
- **零拷贝潜力**：`Serialize(void* V, int64 Length)` 直接操作原始字节，上层可决定拷贝或内存映射。
- **扩展点清晰**：对 UObject 的 `operator<<(UObject*&)` 在 Core 模块中只有空实现/前向声明，实际逻辑下沉到 `CoreUObject`。

#### FMemoryArchive 的内存定位

> 文件：`Engine/Source/Runtime/Core/Public/Serialization/MemoryArchive.h`，第 14~69 行

```cpp
class FMemoryArchive : public FArchive
{
    void Seek(int64 InPos) final { Offset = InPos; }
    int64 Tell() final { return Offset; }
    
    virtual FArchive& operator<<(class FName& N) override
    {
        // FName 以 FString 形式序列化
        FString StringName = N.ToString();
        *this << StringName;
        return *this;
    }
    
    virtual FArchive& operator<<(class UObject*& Res) override
    {
        check(0); // 默认不支持 UObject 指针
        return *this;
    }
protected:
    int64 Offset;
};
```

`FMemoryArchive` 仅维护一个 `Offset`，把 Seek/Tell 的实现从文件 IO 中剥离出来，为所有纯内存派生类提供了统一基座。

### 第 2 层：数据层（How - Structure）

#### UObject 序列化的数据通路

`FObjectWriter` 和 `FObjectReader` 是 CoreUObject 模块对 `FMemoryWriter` / `FMemoryArchive` 的 UObject 特化：

> 文件：`Engine/Source/Runtime/CoreUObject/Public/Serialization/ObjectWriter.h`，第 27~47 行

```cpp
class FObjectWriter : public FMemoryWriter
{
public:
    FObjectWriter(UObject* Obj, TArray<uint8>& InBytes, bool bIgnoreClassRef = false, ...)
        : FMemoryWriter(InBytes)
    {
        ArIgnoreClassRef = bIgnoreClassRef;
        ArIgnoreArchetypeRef = bIgnoreArchetypeRef;
        ArNoDelta = !bDoDelta;
        // ...
        Obj->Serialize(*this);  // <-- 触发 UObject 的序列化虚函数
    }
    
    COREUOBJECT_API virtual FArchive& operator<<(UObject*& Res) override;
    // ... 各种对象指针类型的 operator<<
};
```

> 文件：`Engine/Source/Runtime/CoreUObject/Public/Serialization/ObjectReader.h`，第 28~71 行

```cpp
class FObjectReader : public FMemoryArchive
{
public:
    FObjectReader(UObject* Obj, const TArray<uint8>& InBytes, ...)
        : Bytes(InBytes)
    {
        this->SetIsLoading(true);
        // ...
        Obj->Serialize(*this);  // <-- 从字节流反序列化回 UObject
    }
    
    void Serialize(void* Data, int64 Num)
    {
        if (Offset + Num <= TotalSize())
        {
            FMemory::Memcpy(Data, &Bytes[IntCastChecked<int32>(Offset)], Num);
            Offset += Num;
        }
        else
        {
            SetError();
        }
    }
};
```

**关键数据结构分析**：
- `FObjectWriter` 在构造时立即调用 `Obj->Serialize(*this)`，将所有数据写入绑定的 `TArray<uint8>`。
- `FObjectReader` 构造时同样立即调用 `Obj->Serialize(*this)`，但 `SetIsLoading(true)` 使所有 `operator<<` 走读取分支。
- UObject 指针的序列化不在 Core 层处理，而是在 `FObjectWriter::operator<<(UObject*&)` 中通过 `FLinkerSave` 写入对象索引，或在 `FObjectReader` 中解析索引并查找对象。

#### 代理模式：FObjectAndNameAsStringProxyArchive

> 文件：`Engine/Source/Runtime/CoreUObject/Public/Serialization/ObjectAndNameAsStringProxyArchive.h`，第 21~50 行

```cpp
struct FObjectAndNameAsStringProxyArchive : public FNameAsStringProxyArchive
{
    bool bLoadIfFindFails;
    bool bResolveRedirectors = false;
    
    COREUOBJECT_API virtual FArchive& operator<<(UObject*& Obj) override;
    COREUOBJECT_API virtual FArchive& operator<<(FWeakObjectPtr& Obj) override;
    COREUOBJECT_API virtual FArchive& operator<<(FSoftObjectPtr& Value) override;
    // ...
};
```

这个代理 Archive 的核心价值是**人类可读的序列化**：
- 当 `UObject*` 被序列化时，不写二进制对象索引，而是写 `FSoftObjectPath` 字符串（如 `/Game/Mesh.Cube`）。
- 当 `FName` 被序列化时，不写 Name 表索引，而是写字符串本身。
- 典型用途：Copy&Paste、配置文件（如 `.ini` 的文本格式）、JSON/XML 桥接序列化。

### 第 3 层：逻辑层（How - Behavior）

#### UObject 序列化的调用链

以 `FObjectWriter` 保存一个 `AActor` 为例：

```
FObjectWriter(Obj, Bytes)
  └── Obj->Serialize(Ar)
        └── UActor::Serialize(Ar)  (若重写)
              └── UObject::Serialize(Ar)
                    └── 遍历所有 UPROPERTY
                          └── FProperty::SerializeItem(Ar, ...)
                                └── 基础类型：Ar << Value
                                └── 对象引用：Ar << UObjectPtr
                                      └── FObjectWriter::operator<<(UObject*&)
                                            └── 写入对象索引 / 路径字符串
```

#### FMemoryWriter 的扩容逻辑

> `FMemoryWriter` 的 `Serialize()` 实现位于 `MemoryWriter.h` / `.cpp`，它会调用 `Bytes.AddUninitialized(Num)` 按需扩容。这是典型的**向外部 TArray 写入**的设计，序列化结果直接可用。

#### FArchiveProxy 的包装链

代理类通过持有 `InnerArchive` 引用，将 `Serialize()` 和 `operator<<` 调用转发，同时在特定类型上插入自定义逻辑。这形成了**可组合的序列化管线**：

```
FObjectAndNameAsStringProxyArchive
  └── FNameAsStringProxyArchive
        └── FArchiveProxy
              └── FMemoryWriter (底层字节流)
```

## 上下层关系

| 上层使用者 | 用法 |
|---|---|
| `UObject::Serialize()` | 所有 UObject 派生类的默认序列化入口 |
| `FLinkerLoad` / `FLinkerSave` | 包文件加载/保存时使用文件 Archive |
| `FJsonObjectConverter` | 将 UObject 序列化为 JSON 时，常配合 `FObjectAndNameAsStringProxyArchive` |
| `FConfigCacheIni` | 配置文件读写，使用文本化 Archive |

| 下层依赖 | 说明 |
|---|---|
| `Core` | `FArchive` 基类所在，提供最基础的字节操作 |
| `CoreUObject` | 提供 UObject/FName/FText 的序列化实现 |

## 设计亮点与可迁移经验

1. **统一接口的双向序列化**：通过 `IsLoading()` 标志让同一套代码同时处理读写，避免了维护两套镜像逻辑。
2. **分层解耦**：Core 模块的 `FArchive` 不感知 UObject，所有 UObject 相关逻辑下沉到 `CoreUObject`，保持底层纯净。
3. **代理模式增强行为**：`FArchiveProxy` 家族允许在不修改原始 Archive 的情况下，动态增加"转字符串"、"压缩加密"、"Delta 对比"等行为。
4. **内存与文件统一抽象**：无论是磁盘文件、内存缓冲、网络 Socket 还是加密流，都实现同一套 `FArchive` 接口，上层无感切换。

## 关联阅读

- [[UE-CoreUObject-源码解析：UObject 体系总览]]
- [[UE-CoreUObject-源码解析：反射系统与 UHT]]
- [[UE-CoreUObject-源码解析：Package 与加载]]
- [[UE-构建系统-源码解析：UHT 反射代码生成]]

## 索引状态

- **所属 UE 阶段**：第三阶段 - 核心层 / 3.2 序列化与数据层
- **对应 UE 笔记**：UE-Serialization-源码解析：Archive 序列化体系
- **本轮分析完成度**：✅ 已完成全部三层分析（接口层、数据层、逻辑层）

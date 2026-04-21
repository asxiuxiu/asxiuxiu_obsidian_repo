---
title: UE-Json-源码解析：JSON 与配置序列化
date: 2026-04-17
tags:
  - ue-source
  - engine-architecture
  - json
  - serialization
aliases:
  - UE-Json
---

> [[00-UE全解析主索引|← 返回 00-UE全解析主索引]]

## Why：为什么要理解 UE 的 JSON 体系？

JSON 是 UE 中配置文件、网络协议、编辑器数据交换、存档系统的核心文本格式。UE 不仅在 `Runtime/Json` 中自研了一套 DOM + SAX 风格的解析器，还在 `JsonUtilities` 中通过 `UProperty` 反射桥接，实现了 UStruct/UObject 与 JSON 的自动双向映射。理解这套体系，对开发配置工具、SaveGame 系统、蓝图 JSON 节点至关重要。

## What：Json / JsonUtilities 模块是什么？

UE 将 JSON 能力拆分为两个模块：

1. **`Json` 模块（纯基础设施）**：提供 `FJsonObject`、`FJsonValue`、`FJsonSerializer`、Reader/Writer，**不依赖 UObject**。
2. **`JsonUtilities` 模块（反射桥接）**：提供 `FJsonObjectConverter`，通过 `FProperty` 反射将任意 `UStruct` / `UObject` 与 JSON DOM 互转，依赖 `CoreUObject`。

### 核心类定位

| 类 | 模块 | 职责 |
|---|---|---|
| `FJsonValue` / `FJsonValueNumber` / `FJsonValueString` | Json | JSON 值类型的多态层次 |
| `FJsonObject` | Json | JSON 对象，底层为 `TMap<FString, TSharedPtr<FJsonValue>>` |
| `TJsonReader` / `TJsonWriter` | Json | 基于字符模板的读写器，支持 Pretty / Condensed 打印策略 |
| `FJsonSerializer` | Json | 提供 `Deserialize` / `Serialize` 静态方法，基于策略模板 |
| `FJsonObjectConverter` | JsonUtilities | UStruct ↔ JSON 双向转换核心 |
| `FJsonObjectWrapper` | JsonUtilities | 允许 USTRUCT 中透传原始 JSON 子树 |

## How：JSON 体系的三层源码剖析

### 第 1 层：接口层（What）

#### FJsonObject 的 DOM 接口

> 文件：`Engine/Source/Runtime/Json/Public/Dom/JsonObject.h`，第 22~66 行

```cpp
class FJsonObject
{
public:
    TMap<FString, TSharedPtr<FJsonValue>> Values;

    TSharedPtr<FJsonValue> GetField(FStringView FieldName, EJson JsonType) const
    {
        const TSharedPtr<FJsonValue>* Field = Values.FindByHash(GetTypeHash(FieldName), FieldName);
        if (Field != nullptr && Field->IsValid())
        {
            if (JsonType == EJson::None || (*Field)->Type == JsonType)
            {
                return (*Field);
            }
        }
        return MakeShared<FJsonValueNull>();
    }

    TSharedPtr<FJsonValue> TryGetField(FStringView FieldName) const;
    bool HasField(FStringView FieldName) const;
    void SetField(const FString& FieldName, const TSharedPtr<FJsonValue>& Value);
    // ... GetNumberField / GetStringField / GetArrayField / GetObjectField
};
```

`FJsonObject` 的设计非常直接：一个 `TMap` 包裹 `TSharedPtr<FJsonValue>`，所有 Getter/Setter 都围绕这个 Map 操作。类型安全通过 `EJson` 枚举在运行时检查。

#### FJsonSerializer 的序列化策略

> 文件：`Engine/Source/Runtime/Json/Public/Serialization/JsonSerializer.h`，第 14~90 行

```cpp
struct FJsonSerializerPolicy_JsonObject
{
    using FValue = TSharedPtr<FJsonValue>;
    using FArrayOfValues = TArray<TSharedPtr<FJsonValue>>;
    using FMapOfValues = TSharedPtr<FJsonObject>;

    struct StackState
    {
        EJson Type;
        FString Identifier;
        TArray<TSharedPtr<FJsonValue>> Array;
        TSharedPtr<FJsonObject> Object;
    };
    // ... 基于模板的 ReadBoolean / ReadString / ReadNumber
};
```

`FJsonSerializer` 使用**策略模板 + 栈状态机**解析 JSON：
- `Deserialize` 维护一个 `StackState` 栈，遇到 `{` 压入 Object 状态，遇到 `[` 压入 Array 状态，遇到标量则根据当前栈顶类型决定是 Object 的字段值还是 Array 的元素。
- `Serialize` 遍历 `FJsonObject` / `FJsonValue` 树，通过 `TJsonWriter` 输出字符流。

### 第 2 层：数据层（How - Structure）

#### FJsonValue 的多态类型体系

| 派生类 | 底层数据 | 说明 |
|---|---|---|
| `FJsonValueString` | `FString` | 字符串值 |
| `FJsonValueNumber` | `double` | 数值（默认） |
| `FJsonValueNumberString` | `FString` | 以字符串存储数值，避免精度丢失 |
| `FJsonValueBoolean` | `bool` | 布尔值 |
| `FJsonValueArray` | `TArray<TSharedPtr<FJsonValue>>` | 数组 |
| `FJsonValueObject` | `TSharedPtr<FJsonObject>` | 嵌套对象 |
| `FJsonValueNull` | — | Null |

所有类型通过 `EJson` 枚举区分，多态转换通过 `AsString()` / `AsNumber()` / `AsBool()` / `AsArray()` / `AsObject()` 完成。转换失败时返回安全默认值（如 `0.0`、`false`、空字符串）。

#### JsonUtilities：UStruct ↔ JSON 的反射桥接

> 文件：`Engine/Source/Runtime/JsonUtilities/Public/JsonObjectConverter.h`，第 55~120 行

```cpp
class FJsonObjectConverter
{
public:
    using CustomExportCallback = TDelegate<TSharedPtr<FJsonValue>(FProperty* Property, const void* Value)>;
    using CustomImportCallback = TDelegate<bool(const TSharedPtr<FJsonValue>& JsonValue, FProperty* Property, void* Value)>;

    static JSONUTILITIES_API bool UStructToJsonObject(
        const UStruct* StructDefinition, const void* Struct,
        TSharedRef<FJsonObject> OutJsonObject, int64 CheckFlags = 0, int64 SkipFlags = 0,
        const CustomExportCallback* ExportCb = nullptr, EJsonObjectConversionFlags ConversionFlags = EJsonObjectConversionFlags::None);

    static JSONUTILITIES_API bool JsonObjectToUStruct(
        const TSharedRef<FJsonObject>& JsonObject,
        const UStruct* StructDefinition, void* OutStruct, int64 CheckFlags = 0, int64 SkipFlags = 0,
        const CustomImportCallback* ImportCb = nullptr, bool bStrictMode = false);
};
```

**数据转换规则**：
- **基础类型**：`int`/`float` → `FJsonValueNumber`；`FString`/`FName`/`FText` → `FJsonValueString`。
- **容器类型**：`TArray`/`TSet` → `FJsonValueArray`；`TMap` → `FJsonValueObject`（Key 强制转字符串）。
- **UStruct 嵌套**：递归调用 `UStructToJsonAttributes`。
- **UObject 指针**：默认输出为引用路径字符串；若标记 `CPF_PersistentInstance`，则按值嵌套导出（写入 `_ClassName` 键）。
- **循环引用防护**：通过 `ExportedObjects` 集合记录已导出对象，防止无限递归。

### 第 3 层：逻辑层（How - Behavior）

#### JSON 反序列化调用链（以 UObject 为例）

```
FJsonObjectStringToUStruct(JsonString, OutStruct)
  └── TJsonReader::Tokenize(JsonString)
        └── FJsonSerializer::Deserialize(Reader, JsonObject)
              └── 构建 FJsonObject DOM 树
  └── JsonObjectToUStruct(JsonObject, UStruct, OutStruct)
        └── 遍历 UStruct 的所有 FProperty
              └── JsonValueToUProperty(JsonValue, Property, OutData)
                    ├── 基础类型：直接赋值
                    ├── TArray：解析 JSON Array，逐个元素反序列化
                    ├── TMap：解析 JSON Object，Key-Value 配对
                    └── UStruct：递归 JsonObjectToUStruct
```

#### FJsonSerializer 的栈状态机行为

`Deserialize` 不是递归下降解析，而是**基于栈的迭代解析**：
1. 从 `TJsonReader` 读取 Token（`ObjectStart`、`ArrayStart`、`String`、`Number`、`Boolean`、`Null`、`ObjectEnd`、`ArrayEnd`）。
2. 遇到 `ObjectStart` 时新建 `FJsonObject` 并压栈。
3. 遇到 `ArrayStart` 时新建 `TArray<TSharedPtr<FJsonValue>>` 并压栈。
4. 遇到标量时，根据栈顶类型（Object 或 Array）决定放入当前字段或追加到数组。
5. 遇到 `ObjectEnd` / `ArrayEnd` 时出栈，并将完成的值交给上一层。

这种设计避免了深层嵌套 JSON 的递归栈溢出风险。

## 上下层关系

| 上层使用者 | 用法 |
|---|---|
| `UnrealEd` / `Blutility` | 编辑器数据导出、蓝图 JSON 节点 |
| `SaveGame` 系统 | 存档的文本化序列化 |
| `DerivedDataCache` | 缓存元数据的 JSON 配置 |
| `Localization` | 本地化资源的 JSON 导入导出 |
| `WorldBookmark` / `LevelEditor` | 编辑器状态持久化 |

| 下层依赖 | 说明 |
|---|---|
| `Core` | `FString`、`TMap`、`TArray` 等基础容器 |
| `CoreUObject` | `UStruct`、`FProperty` 反射体系（JsonUtilities 专用） |

## 设计亮点与可迁移经验

1. **纯基础设施与反射桥接分离**：`Json` 模块不依赖 UObject，可被任何底层工具使用；`JsonUtilities` 在其上搭建反射桥，保持关注点清晰。
2. **模板策略的 Reader/Writer**：`TJsonReader<TCHAR>` 和 `TJsonWriter<TCHAR, TPrintPolicy>` 支持不同字符编码和打印格式，扩展性强。
3. **栈式反序列化避免递归溢出**：相比递归下降解析器，`FJsonSerializer` 的栈状态机更适合处理不可信来源的深层嵌套 JSON。
4. **CustomCallback 机制**：`FJsonObjectConverter` 允许注册自定义导出/导入回调，在不修改核心代码的情况下支持日期格式、加密字段等特殊需求。

## 关联阅读

- [[UE-Serialization-源码解析：Archive 序列化体系]]
- [[UE-CoreUObject-源码解析：反射系统与 UHT]]
- [[UE-CoreUObject-源码解析：Class 与 Property 元数据]]

## 索引状态

- **所属 UE 阶段**：第三阶段 - 核心层 / 3.2 序列化与数据层
- **对应 UE 笔记**：UE-Json-源码解析：JSON 与配置序列化
- **本轮分析完成度**：✅ 已完成全部三层分析

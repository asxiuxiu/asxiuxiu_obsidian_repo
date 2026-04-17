---
title: "UE-<模块>-源码解析：<主题>"
date: {{date}}
tags:
  - ue-source
  - engine-architecture
  - chaos-comparison
aliases:
  - "UE <模块> <主题>"
---

> [← 返回 全解析主索引]([[00-引擎与游戏全解析主索引|全解析主索引]])

# UE-<模块>-源码解析：<主题>

## 模块定位

- **UE 模块路径**：`Engine/Source/<Runtime|Editor>/<模块名>/`
- **Build.cs 文件**：`<模块名>.Build.cs`
- **核心依赖**：
- **chaos 对应模块**：

## 接口梳理（第 1 层）

### 公共头文件

| 头文件 | 核心类/结构 | 职责 |
|--------|------------|------|
| `Public/xxx.h` | `Uxxx` | ... |

### 关键 UCLASS/USTRUCT

```cpp
// 示例：带中文注释的 UE 反射标记类
UCLASS(BlueprintType)
class XXX_API AMyActor : public AActor
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere)
    int32 MyProperty;
};
```

## 数据结构（第 2 层）

### 核心类内存布局

### UObject 生命周期 / 状态流转

### 内存分配来源

## 行为分析（第 3 层）

### 关键函数调用链

> 文件：`Engine/Source/.../xxx.cpp`，第 xx~xx 行

```cpp
// 引用关键代码片段
```

### 多线程与同步

### 性能优化手段

## 与上下层的关系

### 上层调用者

### 下层依赖

## UE vs chaos：架构对照

| 维度 | UE 实现 | chaos 实现 | 结论/可迁移经验 |
|------|---------|-----------|----------------|
| 模块边界 | | | |
| 对象模型 | | | |
| 调度/Tick | | | |
| 内存管理 | | | |

## 设计亮点与可迁移经验

1.
2.
3.

## 关键源码片段

## 关联阅读

- chaos 对应笔记：[[xxx-源码解析：xxx]]
- 其他 UE 笔记：[[UE-xxx-源码解析：xxx]]

## 索引状态

- **chaos 对应阶段**：第 X 阶段
- **chaos 对应笔记**：[[xxx-源码解析：xxx]]
- **本轮完成度**：🔄 第一轮 / 🔄 第二轮 / ✅ 第三轮
- **更新日期**：{{date}}

# 源码分析笔记模板

当需要生成游戏引擎源码分析笔记时，参考以下结构。所有笔记必须输出到 `Game/` 目录，并按 `Game/00-引擎与游戏全解析主索引.md` 的阶段分类归档。

## Frontmatter

```yaml
---
title: <标题>
date: YYYY-MM-DD
tags:
  - game-engine-analysis
  - source-analysis
  - <模块名>
  - architecture
aliases:
  - <简短别名>
---
```

## 内容结构

```markdown
# <标题>

> [← 返回 全解析主索引]([[00-引擎与游戏全解析主索引|全解析主索引]])

## 一、模块定位
（物理路径、依赖深度、编译条件、下游依赖）

## 二、公共接口梳理（第 1 层）
（列出核心类、关键 public 方法、头文件清单）

## 三、核心数据结构（第 2 层）
（UML 或表格描述主要结构体/类关系、内存布局推测）

## 四、关键行为分析（第 3 层）
（2~3 个核心函数的调用链、时序图或流程图）

## 五、与上下层的关系
（依赖了谁、被谁依赖、数据流向）

## 六、设计亮点与潜在陷阱
（个人见解、可优化点、常见坑）

## 七、关键源码片段
（贴出最能说明问题的 3~5 段代码，附注释）

## 八、关联阅读
（链接到索引中同阶段或相邻阶段的已有笔记）

---

**索引状态**：所属阶段：<第一阶段~第九阶段>；对应计划笔记：[[索引中的wikilink]]
```

## 代码片段示例

```markdown
> 文件：`project/module/File.hpp`，第 45~62 行

```cpp
class Example {
public:
    // 使用默认分配器
    Example(Allocator* alloc = GetDefaultAllocator());
    
    // 移动构造，保证 noexcept
    Example(Example&& other) noexcept;
};
```
```

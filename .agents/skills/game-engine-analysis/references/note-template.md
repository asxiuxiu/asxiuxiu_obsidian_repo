# 源码分析笔记模板

当需要生成游戏引擎源码分析笔记时，参考以下结构。笔记按引擎输出到对应目录，并按对应引擎全解析主索引的阶段分类归档。

## 按引擎区分的 Frontmatter

### chaos

```yaml
---
title: <模块>-源码解析：<主题>
date: YYYY-MM-DD
tags:
  - game-engine-analysis
  - source-analysis
  - chaos
  - <模块名>
  - architecture
aliases:
  - <简短别名>
---
```

### UE

```yaml
---
title: UE-<模块>-源码解析：<主题>
date: YYYY-MM-DD
tags:
  - ue-source
  - engine-architecture
  - <模块名>
aliases:
  - "UE <模块> <主题>"
---
```

### Bevy

```yaml
---
title: Bevy-<模块>-源码解析：<主题>
date: YYYY-MM-DD
tags:
  - bevy-source
  - engine-architecture
  - ecs
  - <模块名/crate名>
aliases:
  - "Bevy <模块> <主题>"
---
```

## 内容结构

```markdown
# <标题>

> [← 返回 全解析主索引]([[<对应索引文件名>|全解析主索引]])

## 一、模块定位
（物理路径、依赖深度、编译条件、下游依赖）

## 二、公共接口梳理（第 1 层）
（列出核心类/组件、关键 public 方法、接口文件清单）

## 三、核心数据结构（第 2 层）
（UML 或表格描述主要结构体/类/组件关系、内存布局推测）

## 四、关键行为分析（第 3 层）
（2~3 个核心函数/System 的调用链、时序图或流程图）

## 五、与上下层的关系
（依赖了谁、被谁依赖、数据流向）

## 六、设计亮点与潜在陷阱
（个人见解、可优化点、常见坑）

## 七、关键源码片段
（贴出最能说明问题的 3~5 段代码，附注释）

## 八、关联阅读
（链接到索引中同阶段或相邻阶段的已有笔记）

---

**索引状态**：所属阶段：<阶段名>；对应计划笔记：[[索引中的wikilink]]
```

## 代码片段示例

### chaos 示例

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

### UE 示例

```markdown
> 文件：`Engine/Source/Runtime/CoreUObject/Public/UObject/Object.h`，第 120~145 行

```cpp
UCLASS(abstract, config=Engine, BlueprintType)
class COREUOBJECT_API UObject : public UObjectBaseUtility
{
    GENERATED_BODY()
public:
    // UObject 的外层容器，决定生命周期和序列化范围
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Object")
    UObject* Outer;
```
```

### Bevy 示例

```markdown
> 文件：`crates/bevy_ecs/src/world/mod.rs`，第 120~145 行

```rust
pub struct World {
    // 存储所有实体的组件数据
    pub(crate) storages: Storages,
    // 组件元信息注册表
    pub(crate) components: Components,
    // 资源存储
    pub(crate) resources: Resources,
}

impl World {
    /// 生成一个新实体
    pub fn spawn<B: Bundle>(&mut self, bundle: B) -> EntityWorldMut {
        // ...
    }
}
```
```

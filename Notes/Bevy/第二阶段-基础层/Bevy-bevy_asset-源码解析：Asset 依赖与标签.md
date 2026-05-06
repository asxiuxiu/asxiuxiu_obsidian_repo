---
title: Bevy-bevy_asset-源码解析：Asset 依赖与标签
date: 2026-05-06
tags:
  - bevy-source
  - bevy_asset
  - asset-dependency
  - asset-path
  - label
aliases:
  - Bevy Asset 依赖与标签
---

> [[Notes/Bevy/00-Bevy全解析主索引|← 返回 Bevy全解析主索引]]

# Bevy `bevy_asset` 源码解析：Asset 依赖与标签

## 模块定位

游戏资产很少孤立存在。一个材质依赖纹理，一个场景依赖网格和材质，一个动画骨架依赖骨骼绑定。Bevy 的**资产依赖系统**让这种关系被显式建模、自动追踪，并在运行时精确判断"什么时候整个资产树才算真正可用"。

同时，**标签（Label）**机制允许一个物理文件产出多个逻辑资产。一个 GLTF 文件可能包含 5 个 mesh、3 个材质、2 个动画——Bevy 用 `"scene.gltf#Mesh0"` 这样的语法精确定位其中之一。

本节解析 `AssetPath` 的语法设计、`VisitAssetDependencies` 的自动推导机制，以及依赖状态在资产树中的传播算法。

---

## 一、接口层（What）

### 1.1 `AssetPath`：虚拟文件系统的路径抽象

```rust
pub struct AssetPath<'a> {
    source: AssetSourceId<'a>,
    path: CowArc<'a, Path>,
    label: Option<CowArc<'a, str>>,
}
```

> 文件：`crates/bevy_asset/src/path.rs`，第 57~61 行

`AssetPath` 由三个部分组成：

| 部分 | 示例 | 说明 |
|------|------|------|
| `source` | `custom://` | 资产来源标识，默认是 `""`（即 `AssetSourceId::Default`，对应 `assets/` 目录） |
| `path` | `models/player.gltf` | 在来源内部的虚拟路径 |
| `label` | `#Mesh0` | 可选的"子资产"标签，用于从一个物理文件中提取多个逻辑资产 |

完整语法：`source://path/to/file.ext#Label`

```rust
asset_server.load("models/player.gltf#Mesh0");
asset_server.load("remote://textures/diffuse.png");
```

### 1.2 `AssetId` 与 `UntypedAssetId`

```rust
pub enum AssetId<A: Asset> {
    Index { index: AssetIndex, marker: PhantomData<fn() -> A> },
    Uuid { uuid: Uuid },
}
```

> 文件：`crates/bevy_asset/src/id.rs`，第 22~44 行

`AssetId` 是资产的**运行时标识符**，分为两类：

- **`Index`**：由 `AssetIndexAllocator` 分配的世代索引，是默认路径。它足够小（`u64`），可以作为数组索引高效查找到 `DenseAssetStorage`；
- **`Uuid`**：跨运行稳定的 UUID，用于编译期引用或序列化场景。存储在 `HashMap<Uuid, A>` 中，访问稍慢但具有持久性。

`UntypedAssetId` 则是类型擦除版本，把 `TypeId` 存储在枚举内部，用于跨资产类型的比较和哈希。

### 1.3 `VisitAssetDependencies`：自动依赖推导

```rust
pub trait VisitAssetDependencies {
    fn visit_dependencies(&self, visit: &mut impl FnMut(UntypedAssetId));
}
```

> 文件：`crates/bevy_asset/src/lib.rs`，第 467~469 行

`#[derive(Asset)]` 会自动实现 `VisitAssetDependencies`：所有标记了 `#[dependency]` 的 `Handle<T>` 字段都会被遍历。用户也可以手动实现：

```rust
impl VisitAssetDependencies for MyMaterial {
    fn visit_dependencies(&self, visit: &mut impl FnMut(UntypedAssetId)) {
        self.diffuse_map.visit_dependencies(visit);
        self.normal_map.visit_dependencies(visit);
    }
}
```

---

## 二、数据层（How - Structure）

### 2.1 `AssetPath` 的解析机

`AssetPath::parse_internal` 是一个纯手写的字符状态机：

```rust
fn parse_internal(asset_path: &str)
    -> Result<(Option<&str>, &Path, Option<&str>), ParseAssetPathError>
{
    // 1. 扫描 `://` 确定 source
    // 2. 扫描最后一个 `#` 确定 label
    // 3. 中间部分为 path
    // 4. 验证：source 中不能有 `#`，label 中不能有 `://`
}
```

> 文件：`crates/bevy_asset/src/path.rs`，第 138~224 行

解析策略：

- **`://` 的首次出现**定义 source 边界；
- **`#` 的最后一次出现**定义 label 边界（因为文件名本身可能包含 `#`）；
- 为了处理文件名含 `#` 的情况，提供了 `From<&'static Path>` 和 `AssetPath::from_path` 以绕过字符串解析。

### 2.2 路径解析（Resolution）

`AssetPath` 支持相对路径解析：

```rust
let base = AssetPath::parse("a/b");
base.resolve(&AssetPath::parse("c"));        // → "a/b/c"
base.resolve(&AssetPath::parse("../c"));     // → "a/c"
base.resolve(&AssetPath::parse("/c"));       // → "c"（绝对路径）
base.resolve(&AssetPath::parse("#label"));   // → "a/b#label"（仅替换 label）
```

以及嵌入语义（RFC 1808）：

```rust
base.resolve_embed(&AssetPath::parse("c"));  // → "a/c"（去掉 base 的文件名部分）
```

> 文件：`crates/bevy_asset/src/path.rs`，第 373~426 行

这对 Loader 递归加载相对路径依赖至关重要。例如 GLTF Loader 读取 `"textures/diffuse.png"` 时，会自动相对于 GLTF 文件路径解析。

### 2.3 依赖在 `AssetInfo` 中的存储

```rust
pub(crate) struct AssetInfo {
    weak_handle: Weak<StrongHandle>,
    load_state: LoadState,
    dep_load_state: DependencyLoadState,
    rec_dep_load_state: RecursiveDependencyLoadState,
    loading_dependencies: HashSet<ErasedAssetIndex>,
    failed_dependencies: HashSet<ErasedAssetIndex>,
    loading_rec_dependencies: HashSet<ErasedAssetIndex>,
    failed_rec_dependencies: HashSet<ErasedAssetIndex>,
    dependents_waiting_on_load: HashSet<ErasedAssetIndex>,
    dependents_waiting_on_recursive_dep_load: HashSet<ErasedAssetIndex>,
}
```

> 文件：`crates/bevy_asset/src/server/info.rs`，第 26~50 行

每个 `AssetInfo` 同时维护了**依赖方**和**被依赖方**的两组信息：

- **`loading_dependencies`**：我正在等待哪些资产加载完成；
- **`dependents_waiting_on_load`**：有哪些资产正在等待我加载完成。

这是一种**双向链表**的简化版，让状态传播可以沿着依赖图的边双向行走。

### 2.4 `LoadedAsset` 中的依赖数据结构

```rust
pub struct LoadedAsset<A: Asset> {
    pub(crate) value: A,
    pub(crate) dependencies: HashSet<ErasedAssetIndex>,      // Handle 引用的资产
    pub(crate) loader_dependencies: HashMap<AssetPath<'static>, AssetHash>, // 加载时读取的辅助文件
    pub(crate) labeled_assets: Vec<LabeledAsset>,
    pub(crate) label_to_asset_index: HashMap<CowArc<'static, str>, usize>,
    pub(crate) asset_id_to_asset_index: HashMap<UntypedAssetId, usize>,
}
```

> 文件：`crates/bevy_asset/src/loader.rs`，第 144~157 行

- **`dependencies`** 用于**运行时引用追踪**：确保材质使用的纹理已加载；
- **`loader_dependencies`** 用于**构建系统/热重载追踪**：记录加载过程中读取了哪些辅助文件（如 `.gltf` 引用的 `.bin`），当辅助文件变更时触发重新处理。

---

## 三、逻辑层（How - Behavior）

### 3.1 `AssetPath` 解析流程

```mermaid
graph LR
    A["\"custom://dir/file.txt#label\""] --> B[扫描字符]
    B --> C{遇到 '://' ?}
    C -->|首次| D[source = "custom"]
    C -->|否| E{遇到 '#' ?}
    E -->|最后| F[label = "label"]
    E -->|否| G[继续扫描]
    D --> E
    G --> E
    F --> H[path = "dir/file.txt"]
    H --> I[验证: source 无 '#', label 无 '://']
```

> 文件：`crates/bevy_asset/src/path.rs`，第 138~224 行

解析器故意采用**单遍扫描**而非正则表达式，因为 Rust 的正则库较重，而资产路径解析是高频操作（每帧可能成百上千次）。

### 3.2 依赖收集：`finish` 时的自动扫描

当 Loader 调用 `load_context.finish(asset)` 时：

```rust
pub fn finish<A: Asset>(mut self, value: A) -> LoadedAsset<A> {
    value.visit_dependencies(&mut |asset_id| {
        if let UntypedAssetId::Index { type_id, index } = asset_id {
            self.dependencies.insert(ErasedAssetIndex { index, type_id });
        }
    });
    LoadedAsset { value, dependencies: self.dependencies, ... }
}
```

> 文件：`crates/bevy_asset/src/loader.rs`，第 534~558 行

`visit_dependencies` 是递归的：如果 `MyMaterial` 包含 `Handle<Image>`，`Image` 的 `Handle` 会被访问；如果 `MyScene` 包含 `Handle<MyMaterial>`，则递归访问到 `MyMaterial` 内部的 `Handle<Image>`。但最终写入 `dependencies` 的只有**直接依赖**（即 `Handle` 的 ID），因为 `AssetServer` 会在这些直接依赖加载完成后，再递归检查它们的依赖。

### 3.3 依赖状态传播算法

这是资产系统最精妙的算法之一。假设资产 A 依赖 B，B 依赖 C：

```
C 加载完成
  │
  ▼
process_asset_load(C)
  │
  ├── C.rec_dep_load_state = Loaded
  ├── 检查 dependents_waiting_on_recursive_dep_load: [B]
  │
  ▼
propagate_loaded_state(C → B)
  │
  ├── B.loading_rec_dependencies.remove(C) → 空
  ├── B.rec_dep_load_state = Loaded
  ├── B.load_state 可能还是 Loading（自身字节还没读完）
  │
  ▼
  若 B 的 load_state 也已是 Loaded:
    发送 InternalAssetEvent::LoadedWithDependencies(B)
    检查 B 的 dependents_waiting_on_recursive_dep_load: [A]
    propagate_loaded_state(B → A)
```

> 文件：`crates/bevy_asset/src/server/info.rs`，第 579~610 行

关键点：**`rec_dep_load_state` 和 `load_state` 是两个独立的条件**，只有两者都为 `Loaded` 时，才会触发 `LoadedWithDependencies`。这让"资产本身已加载"和"资产的所有递归依赖都已加载"成为两个可以独立观察的状态。

### 3.4 Label 子资产的查找

当用户请求 `"scene.gltf#Mesh0"` 时：

1. `AssetServer::load_internal` 先以 `"scene.gltf"` 为 base path 加载根资产；
2. Loader 在 `LoadContext` 中通过 `add_loaded_labeled_asset("Mesh0", mesh)` 注册子资产；
3. `load_internal` 完成后，从 `LoadedAsset::label_to_asset_index` 中找到 `"Mesh0"` 对应的 `LabeledAsset`；
4. 返回该子资产的 `Handle<Mesh>`。

> 文件：`crates/bevy_asset/src/server/mod.rs`，第 881~928 行

如果请求的子资产不存在，`load_internal` 会发送 `InternalAssetEvent::Failed`，并返回 `MissingLabel` 错误，同时附带上该文件所有可用的 label 列表，方便开发者调试。

### 3.5 `loader_dependents`：热重载的反向依赖图

当 `watching_for_changes` 为 `true` 时，`AssetInfos` 会额外维护：

```rust
pub(crate) loader_dependents: HashMap<AssetPath<'static>, HashSet<AssetPath<'static>>>,
```

这记录了：**哪些资产在加载过程中读取了某个辅助文件**。例如：

- `scene.gltf` 在加载时读取了 `scene.bin`；
- 那么 `loader_dependents["scene.bin"]` 中就包含 `"scene.gltf"`。

当 `scene.bin` 被修改时，`AssetProcessor` 就知道 `scene.gltf` 也需要重新处理。

> 文件：`crates/bevy_asset/src/server/info.rs`，第 504~518 行

---

## 四、上下层关系

| 方向 | 交互对象 | 方式 |
|------|---------|------|
| **下层** | `std::path::Path` / `PathBuf` | `AssetPath` 内部持有 `CowArc<Path>` |
| **下层** | `bevy_platform::collections::HashMap` | 路径索引、依赖集合 |
| **同层** | `AssetServer` / `AssetLoader` | `LoadContext` 提供相对路径解析 |
| **同层** | `AssetProcessor` | `loader_dependencies` 驱动预处理变更检测 |
| **上层** | `bevy_gltf` | GLTF 是最典型的多 label 资产来源 |
| **上层** | `bevy_scene` | 场景序列化存储 `AssetPath` 字符串 |

---

## 五、设计亮点

1. **AssetPath 的三元组设计**：`source`、`path`、`label` 的分离让"同一个物理文件产出多个逻辑资产"变得自然，同时支持多来源（文件系统、网络、内存、嵌入资源）；
2. **双向依赖追踪**：`loading_dependencies` + `dependents_waiting_on_load` 让状态传播既可以"自底向上"（依赖完成通知被依赖方），也可以在未来扩展"自顶向下"（取消加载时级联取消）；
3. **自动依赖推导**：`#[derive(Asset)]` 通过 proc macro 自动生成 `VisitAssetDependencies`，消除了手动维护依赖列表的心智负担；
4. **`loader_dependencies` 与 `dependencies` 的区分**：前者服务于构建系统（什么文件变更会触发重新处理），后者服务于运行时（什么资产必须就绪才能使用）；
5. **世代索引的跨类型复用**：`AssetIndex` 只有 `index + generation`，真正的类型信息在 `AssetId` 的编译期泛型参数和 `UntypedAssetId` 的 `TypeId` 中。这让同一块存储可以被不同资产类型的 allocator 独立管理。

---

## 六、关键源码片段

### `AssetPath::parse_internal` — 单遍扫描解析

> 文件：`crates/bevy_asset/src/path.rs`，第 138~224 行

```rust
fn parse_internal(asset_path: &str)
    -> Result<(Option<&str>, &Path, Option<&str>), ParseAssetPathError>
{
    let mut source_range = None;
    let mut path_range = 0..asset_path.len();
    let mut label_range = None;
    let mut source_delimiter_chars_matched = 0;
    let mut last_found_source_index = 0;

    for (index, char) in asset_path.char_indices() {
        match char {
            ':' => source_delimiter_chars_matched = 1,
            '/' => {
                match source_delimiter_chars_matched {
                    2 => {
                        if source_range.is_none() {
                            if label_range.is_some() {
                                return Err(ParseAssetPathError::InvalidSourceSyntax);
                            }
                            source_range = Some(0..index - 2);
                            path_range.start = index + 1;
                        }
                        last_found_source_index = index - 2;
                        source_delimiter_chars_matched = 0;
                    }
                    _ => {}
                }
            }
            '#' => {
                path_range.end = index;
                label_range = Some(index + 1..asset_path.len());
                source_delimiter_chars_matched = 0;
            }
            _ => source_delimiter_chars_matched = 0,
        }
    }
    // ... 验证与返回
}
```

### `AssetInfos::propagate_loaded_state` — 递归依赖传播

> 文件：`crates/bevy_asset/src/server/info.rs`，第 579~610 行

```rust
fn propagate_loaded_state(
    infos: &mut AssetInfos,
    loaded_id: ErasedAssetIndex,
    waiting_id: ErasedAssetIndex,
    sender: &Sender<InternalAssetEvent>,
) {
    let dependents_waiting_on_rec_load = if let Some(info) = infos.get_mut(waiting_id) {
        info.loading_rec_dependencies.remove(&loaded_id);
        if info.loading_rec_dependencies.is_empty() && info.failed_rec_dependencies.is_empty() {
            info.rec_dep_load_state = RecursiveDependencyLoadState::Loaded;
            if info.load_state.is_loaded() {
                sender.send(InternalAssetEvent::LoadedWithDependencies { index: waiting_id })
                    .unwrap();
            }
            Some(core::mem::take(&mut info.dependents_waiting_on_recursive_dep_load))
        } else { None }
    } else { None };

    if let Some(dependents) = dependents_waiting_on_rec_load {
        for dep_id in dependents {
            Self::propagate_loaded_state(infos, waiting_id, dep_id, sender);
        }
    }
}
```

---

## 七、关联阅读

- [[Bevy-bevy_asset-源码解析：AssetServer 与 Handle]] — Handle 与 AssetId 的关系、世代索引分配器
- [[Bevy-bevy_asset-源码解析：AssetLoader 与加载管线]] — LoadContext 如何注册子资产和依赖
- [[Bevy-bevy_asset-源码解析：AssetEvents 与热重载]] — `loader_dependents` 与 `AssetProcessor` 的协作
- [[Bevy-专题：资源加载全链路]] — 从 `AssetPath` 到 GPU 提交的端到端分析

---

> **索引状态**：本笔记属于第二阶段「基础层与反射系统」→ 2.2 资产与加载（bevy_asset）。对应索引中的 `[[Bevy-bevy_asset-源码解析：Asset 依赖与标签]]`。

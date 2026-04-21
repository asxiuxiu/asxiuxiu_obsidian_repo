---
title: UI-System-架构解析：终端渲染框架
description: 从 React 到终端字符，理解 Claude Code 如何用自研渲染引擎把 AI 的内部状态变成可交互的画面
tags:
  - agent
  - source-analysis
  - ui
---

> [[Notes/Agent/索引|← 返回 Agent 索引]]

# UI-System-架构解析：终端渲染框架

## Why：为什么要理解 UI 渲染与事件系统？

### 问题背景

AI 在"想"、在"调用工具"、在"等待审批"——这些内部状态对用户来说是黑盒。如果没有一套渲染链路，用户只能盯着一片空白，不知道 Agent 是卡住了还是在努力工作。

在游戏引擎里，这个问题会以更复杂的形式出现：
- `PhysicsAgent` 正在调整 100 个刚体的参数，但编辑器里看不到任何反馈
- `GameplayAgent` 的请求被审批系统挂起，但你不知道它在等什么
- 多 Agent 并行执行时，某个子 Agent 崩溃了你却无从得知

### 不用专用渲染链路的后果

1. **AI 成了黑箱**：用户/开发者无法建立对 Agent 的信任，稍微慢一点就想按 Ctrl+C
2. **调试成本极高**：Agent 说"我已经改了"，但你看不到改前后的对比，只能手动验证
3. **多端适配灾难**：如果把 UI 逻辑和 AI 逻辑耦合在一起，换一个平台（从 Editor 内嵌面板换到 Web 调试器）就要重写整套交互

### 应用场景

1. **实时观察 Agent 思维链**：在编辑器侧边栏显示当前 Agent 的推理步骤和即将调用的工具
2. **可视化文件改动**：像 Claude Code 的 diff 视图一样，把 Agent 的修改以颜色高亮的方式实时呈现
3. **跨平台状态同步**：同一套 AI 执行状态，既能在 Editor 面板显示，也能通过 WebSocket 推送到浏览器调试器

> [!tip] 从对话框出发
> 在引入专用渲染链路之前，AI 的输出只是一段纯文本。引入之后，AI 的每一个内部动作——开始思考、调用工具、收到结果、遇到错误——都可以被映射为结构化的"显示块"，并以最适合当前终端的形式渲染出来。这个质变藏在"事件流 + 显示协议"的分离架构里。

---

## What：终端 UI 渲染系统的本质是什么？

### 核心定义

**终端 UI 渲染系统**是把程序的抽象状态树（React 组件树、事件对象、数据结构）转化为终端可显示的字符矩阵，并在状态变化时以最小代价更新终端画面的全套基础设施。

### 关键概念速查

| 概念 | 作用 | 在 Agent 中的体现 |
|-----|------|-----------------|
| **DOM 抽象** | 用树形结构描述 UI 组件的层级关系 | Ink 的 `ink-root` / `ink-box` / `ink-text` |
| **Layout 引擎** | 计算每个节点在屏幕上的位置和大小 | Yoga（Facebook 的 C++ 布局引擎） |
| **Screen 缓冲区** | 二维字符数组，代表当前帧要显示的画面 | `Screen` 对象，存储字符 + 样式 + hyperlink |
| **Diff 引擎** | 比较前后两帧的差异，只更新变化的部分 | `log-update` 的终端转义序列生成 |
| **DisplayBlock** | 结构化的显示单元，与具体渲染端解耦 | Kimi CLI 的 `DiffDisplayBlock`、`ShellDisplayBlock` |
| **Wire 协议** | 把内部事件序列化为标准消息流 | `TurnBegin` / `StepBegin` / `ToolCallRequest` |

### 架构图解

Claude Code 的 Ink 渲染管线：

```
React 组件树
    ↓
Ink Reconciler (自定义 reconciler.ts)
    ↓
DOM 节点树 (ink-root / ink-box / ink-text)
    ↓
Yoga Layout (calculateLayout)
    ↓
renderNodeToOutput() → Output 操作列表
    ↓
Output.get() → Screen 缓冲区
    ↓
log-update.diff(prevScreen, nextScreen) → 终端转义序列
    ↓
stdout 写入
```

Kimi CLI 的渲染模型：

```
Agent 执行逻辑
    ↓
生成 DisplayBlock (Diff/Shell/Todo/BackgroundTask)
    ↓
Wire 事件流 (Event / Request)
    ↓
客户端接收并渲染（Web UI / Terminal / IDE 插件）
```

---

---

前面我们搞懂了 **终端 UI 渲染系统是把程序的抽象状态树转化为终端可显示字符矩阵的全套基础设施**。现在我们要回答的问题是：**Claude Code 和 Kimi CLI 分别是怎样把 AI 的内部状态变成用户能看到的画面的？** Claude 像一家电视台，自己在地下室建了一整套演播设备（Ink 渲染引擎）；Kimi 像一家内容制作公司，只负责拍出"视频素材块"（DisplayBlock），具体怎么播交给各个平台（Web、CLI、IDE 插件）自己决定。接下来我们把源码拆开，一层层看。

---

## How：不同 Agent 工具是如何实现的？

### 1. 宏观对比：Claude Code vs Kimi CLI

**先给一个总体的直觉比喻**：

> 想象你要展示一场演唱会的现场画面。Claude Code 的做法是在地下室自建一整套电视台：从摄像机（React 组件）、导播台（Reconciler）、画面布局（Yoga 布局引擎）、到信号发射（终端转义序列），全部自己搞定。Kimi CLI 的做法是只负责拍出标准化的"视频片段"（DisplayBlock），然后把素材通过网络发给电视台、网站、手机 App，让它们自己决定怎么播。

接下来再展开细节对比：

| 维度 | Claude Code (Ink) | Kimi CLI (DisplayBlock + Wire) | 差异原因分析 |
|------|-------------------|-------------------------------|-------------|
| **核心抽象** | 终端内的 React 渲染引擎，像一家自建的电视台 | 服务端生成结构化显示块，客户端负责渲染，像一家内容制作公司 | Claude Code 是完整 TUI 应用；Kimi 是客户端-服务端分离架构 |
| **渲染管线** | DOM → Yoga → Screen → Diff → Terminal，像电视台的完整制播流程 | Agent 逻辑 → DisplayBlock → Wire JSON → 多端消费，像拍素材发给各平台 | Ink 追求终端原生体验；Kimi 追求跨平台一致性 |
| **状态更新粒度** | 基于 Yoga 节点 dirty 标记的增量渲染，像导播台只切有变化的镜头 | 基于 Wire 事件的整消息更新，像每次发一整个视频片段 | 终端有严格的刷新性能要求；Wire 客户端可以自己做局部优化 |
| **交互能力** | 完整鼠标、键盘、选区、滚动、焦点管理，像专业剪辑软件 | 依赖客户端实现对 Wire 事件的响应，像把素材包交给第三方剪辑 | Ink 自己控制全部交互；Kimi 把交互委托给客户端 |
| **对引擎的启示** | 如果引擎 Editor 是单一桌面端，可以直接内嵌类似 Ink 的 React 渲染层 | 如果引擎需要支持 Web 调试器、远程监控、CLI，应该优先采用 DisplayBlock + Wire 模式 | 引擎的 AI 集成应该优先考虑**跨端消费** |

> **用人话讲**：Claude Code 的 Ink 框架像一家电视台，从拍摄、剪辑、播控、发射全部自建，只为把最好的画面送到你家电视机（终端）。而 Kimi CLI 像 Netflix，只负责制作标准化的视频文件（DisplayBlock），然后让各种设备（手机、平板、电视）自己解码播放。如果你的引擎需要支持多种客户端（Editor、Web、CLI），Kimi 的模式更值得借鉴。

> [!warning] 如果 Claude Code 不用 Ink 会怎样？
> 如果 Claude Code 直接用普通的 `console.log` 输出，那么当 AI 同时调用多个工具、更新多个文件时，终端画面会乱成一团——旧的输出被新的输出覆盖，用户根本看不清发生了什么。Ink 的双缓冲和 Diff 机制确保了"只有变化的部分才会更新"，终端画面不会闪烁或抖动。

> [!warning] 如果 Kimi CLI 不做 DisplayBlock 会怎样？
> 如果 Kimi CLI 直接把终端渲染逻辑和 Agent 逻辑耦合在一起，那么每支持一个新客户端（比如 Web UI 或 VS Code 插件），就得重写一套渲染代码。DisplayBlock + Wire 协议让"内容生产"和"内容消费"彻底解耦，新客户端只需要"听懂"同一套 JSON 格式。

### 2. 核心机制伪代码

#### Claude Code Ink 的渲染循环

这段伪代码模拟了 Ink 如何把 React 组件树变成终端上的字符画面。

```typescript
class Ink {
  rootNode: DOMElement     // 自定义 DOM 树
  frontFrame: Frame        // 上一帧画面
  backFrame: Frame         // 当前帧画面
  
  onRender() {
    // 1. Yoga 布局（在 React commit 阶段已计算好）
    
    // 2. 把 DOM 树渲染为 Screen 操作
    const frame = this.renderer({
      frontFrame: this.frontFrame,
      backFrame: this.backFrame,
      terminalWidth, terminalRows,
      altScreen: this.altScreenActive
    })
    
    // 3. 应用选区/搜索高亮覆盖层
    applySelectionOverlay(frame.screen, this.selection)
    applySearchHighlight(frame.screen, this.searchHighlightQuery)
    
    // 4. 计算前后帧差异，生成终端转义序列
    const diff = this.log.render(this.frontFrame, frame, this.altScreenActive)
    
    // 5. 优化并写入终端
    const optimized = optimize(diff)
    writeDiffToTerminal(this.terminal, optimized)
    
    // 6. 交换缓冲区
    this.backFrame = this.frontFrame
    this.frontFrame = frame
  }
}
```

**这段代码在做什么**：Ink 的渲染循环就像一个电视台的导播室。第一步，导演已经通过 Yoga 算好了每个画面元素的位置；第二步，把这些元素绘制成当前帧的画面；第三步，加上选区高亮和搜索高亮（就像直播时的字幕条）；第四步，对比上一帧和当前帧的差异，只生成变化部分的控制指令；第五步，把优化后的指令发送到终端；第六步，交换前后帧缓冲区，准备下一轮。

**核心设计思想**：
- **双缓冲**：用前后两帧画面做对比，避免直接修改正在显示的画面
- **Diff 驱动**：只更新变化的部分，减少终端刷新的开销
- **增量布局**：只有 dirty 的 Yoga 节点才会重新计算布局

#### Kimi CLI 的 Wire + DisplayBlock 模型

这段伪代码模拟了 Kimi CLI 如何生成标准化的"视频片段"（DisplayBlock），而不关心终端怎么画。

```python
# Agent 执行过程中生成显示块
def on_tool_call(tool_name, args):
    if tool_name == "Shell":
        yield ShellDisplayBlock(
            type="shell",
            language="bash",
            command=args["command"]
        )
    
def on_file_diff(path, old, new):
    yield DiffDisplayBlock(
        type="diff",
        path=path,
        old_text=old,
        new_text=new
    )

# Wire 协议把这些块包装成事件流
event = StepBegin(n=1)
yield event
yield StatusUpdate(context_usage=0.45)
yield DiffDisplayBlock(...)
yield StepEnd()
```

**这段代码在做什么**：Kimi CLI 不操心终端怎么画，它只负责定义"这是什么类型的内容"。当 AI 调用 Shell 工具时，生成一个 `ShellDisplayBlock`；当 AI 修改了文件时，生成一个 `DiffDisplayBlock`。这些内容通过 Wire 协议包装成事件流，发送给各个客户端。客户端收到后，可以决定把 `DiffDisplayBlock` 渲染成终端的 diff 视图、Web 的 side-by-side 对比、或者 IDE 插件的代码高亮。

**核心设计思想**：
- **内容与渲染解耦**：Agent 只生产结构化内容，客户端决定如何渲染
- **标准化事件流**：所有状态变化都通过 Wire 协议序列化
- **多端统一消费**：同一套事件流可以被 Web UI、CLI、IDE 插件同时消费

### 3. 关键源码印证

#### Ink 的自定义 Reconciler

**这段代码在做什么**：Claude Code 没有使用 React DOM，而是自己实现了一个 `react-reconciler`，把 React 组件映射到自定义的 `ink-*` 节点树。这就像在 React 和终端之间建了一座翻译桥。

```typescript
// D:/workspace/claude-code-main/src/ink/reconciler.ts (第 224~315 行)
const reconciler = createReconciler({
  createInstance(type, props) {
    const node = createNode(type)  // ink-root / ink-box / ink-text
    for (const [key, value] of Object.entries(props)) {
      applyProp(node, key, value)  // 设置 style / attributes / event handlers
    }
    return node
  },
  appendChild: appendChildNode,
  insertBefore: insertBeforeNode,
  removeChild(node, removeNode) {
    removeChildNode(node, removeNode)
    cleanupYogaNode(removeNode)
  },
  commitUpdate(node, type, oldProps, newProps) {
    const props = diff(oldProps, newProps)
    // ... 应用属性变更，触发 markDirty
  },
  resetAfterCommit(rootNode) {
    rootNode.onComputeLayout?.()  // Yoga 布局
    rootNode.onRender?.()          // 触发渲染帧
  }
})
```

**为什么这样设计**：标准的 React DOM 是为浏览器设计的，它输出的是 HTML 元素，而终端需要的是字符矩阵和转义序列。Ink 的自定义 Reconciler 让 React 组件可以直接"说话"给终端听，而不需要浏览器的参与。这就像给一位只会说英语的导演配了一位会说终端语言的同声传译。

#### DOM 节点与 Yoga 的绑定

**这段代码在做什么**：Ink 为每个 `ink-box` 和 `ink-text` 节点都绑定了一个 Yoga 布局节点。Yoga 是 Facebook 开发的 C++ 布局引擎，负责计算每个节点在屏幕上的位置和大小。

```typescript
// D:/workspace/claude-code-main/src/ink/dom.ts (第 110~132 行)
export const createNode = (nodeName: ElementNames): DOMElement => {
  const needsYogaNode =
    nodeName !== 'ink-virtual-text' &&
    nodeName !== 'ink-link' &&
    nodeName !== 'ink-progress'
  const node: DOMElement = {
    nodeName,
    style: {},
    attributes: {},
    childNodes: [],
    parentNode: undefined,
    yogaNode: needsYogaNode ? createLayoutNode() : undefined,
    dirty: false,
  }

  if (nodeName === 'ink-text') {
    node.yogaNode?.setMeasureFunc(measureTextNode.bind(null, node))
  }
  return node
}
```

**为什么这样设计**：终端的排版计算非常复杂——要考虑换行、溢出、flex 布局等。Yoga 把这些复杂度封装好了，Ink 只需要把 `ink-box` 的样式属性翻译成 Yoga 的 API 调用即可。`dirty` 标记则确保了只有发生变化的节点才会触发重新布局，避免每一帧都全量计算。

#### 渲染到 Screen 的 blit 优化

**这段代码在做什么**：如果 Ink 发现某个节点的内容、位置、大小都没变，它会直接从前一帧的 Screen 缓冲区"整块复制"（blit）过来，而不是重新测量和绘制文字。

```typescript
// D:/workspace/claude-code-main/src/ink/render-node-to-output.ts (第 454~482 行)
const cached = nodeCache.get(node)
if (
  !node.dirty &&
  !skipSelfBlit &&
  node.pendingScrollDelta === undefined &&
  cached &&
  cached.x === x && cached.y === y &&
  cached.width === width && cached.height === height &&
  prevScreen
) {
  output.blit(prevScreen, fx, fy, fw, fh)
  return
}
```

**为什么这样设计**：终端的刷新性能非常敏感。如果每一帧都重新渲染所有文字，即使是一个很小的终端窗口也扛不住 60fps。blit 优化让 Ink 可以像游戏引擎的"帧复用"一样，只绘制变化的部分。这是终端能保持流畅滚动的核心秘诀。

#### Kimi CLI 的 DisplayBlock

**这段代码在做什么**：Kimi CLI 定义了几种常见的 DisplayBlock 类型：`DiffDisplayBlock`（文件差异）、`ShellDisplayBlock`（Shell 命令）、`BackgroundTaskDisplayBlock`（后台任务状态）。它完全不关心这些块最终在终端怎么画。

```python
// D:/workspace/kimi-cli-main/src/kimi_cli/tools/display.py (第 7~46 行)
class DiffDisplayBlock(DisplayBlock):
    type: str = "diff"
    path: str
    old_text: str
    new_text: str

class ShellDisplayBlock(DisplayBlock):
    type: str = "shell"
    language: str
    command: str

class BackgroundTaskDisplayBlock(DisplayBlock):
    type: str = "background_task"
    task_id: str
    kind: str
    status: str
    description: str
```

**为什么这样设计**：Kimi CLI 的核心哲学是"服务端生成结构化内容，客户端自由渲染"。`DisplayBlock` 就像 HTML 的标签（`<div>`、`<img>`、`<pre>`）——它定义了语义，但不定义样式。终端客户端可以把 `ShellDisplayBlock` 渲染成带语法高亮的代码块，Web 客户端可以渲染成可复制的命令卡片，IDE 插件可以渲染成可点击的运行按钮。这种解耦让 Kimi CLI 可以无缝支持多种客户端，而不需要为每种客户端写专门的渲染逻辑。

## 引擎映射：这个设计对我的游戏引擎有什么启发？

### 1. 对应系统

我的引擎里最接近这个模块的是 **AgentBridge 的反馈协议**和**编辑器 AI 面板**。目前 `AgentBridge` 只返回 JSON 字符串（如 `{"status":"ok"}`），这相当于 Claude Code 没有 Ink、只有 `console.log`——AI 能操作引擎，但引擎没有任何结构化的方式把内部状态反馈给 AI 或用户。

### 2. 可借鉴点

**借鉴 1：采用 DisplayBlock 模式而非直接操作 UI**

不要像 Ink 那样让 AI 的反馈直接绑定到 Editor 的某个 React 组件上。而应该像 Kimi CLI 一样，在中间插入一层 **EngineDisplayBlock**：

```cpp
struct EngineDisplayBlock {
    std::string type;  // "diff", "log", "profile", "screenshot", "task_status"
    std::string jsonPayload;
};
```

这样：
- Editor 内嵌面板可以把它渲染成 React 组件
- Web 调试器可以把它渲染成 HTML
- CLI 工具可以直接打印成文本
- AI 自己也能消费这些结构块来闭环决策

**借鉴 2：把 Agent 执行过程抽象为事件流**

`AgentBridge` 不应该只是"调用一个工具、等一个 JSON 返回"。AI 执行引擎任务的过程应该产生一个**事件流**：

```cpp
enum class AgentEventType {
    TurnBegin,
    StepBegin,
    ToolCall,       // AI 决定调用引擎工具
    ToolResult,     // 工具执行完毕
    DisplayBlock,   // 结构化状态反馈
    StepEnd,
    TurnEnd
};
```

这个事件流既可以用于：
- 实时更新 Editor UI
- 持久化到日志供事后分析
- 作为上下文压缩的边界（一个 Step 结束后可以摘要）

### 3. 审视与修正行动项

> [!warning] SelfGameEngine 设计修正
> 
> 打开 `Notes/SelfGameEngine/从零开始的引擎骨架`，AgentBridge 目前的设计存在以下盲区：
> 
> 1. **反馈通道过于简陋**：`ToolResult` 只有一个 `json` 字符串，没有区分"状态反馈"和"显示反馈"。当 AI 调用 `step(dt)` 后，引擎应该返回一个 `DisplayBlock` 告诉它这一帧发生了什么（ChangeLog、性能分析、截图）。
> 
> 2. **没有事件流模型**：原文提到 AgentBridge 应该是"流式协议"，但没有定义事件类型和生命周期。根据 Kimi CLI 的 Wire 协议，需要补充 `TurnBegin/StepBegin/ToolCall/ToolResult/DisplayBlock/StepEnd` 的完整事件模型。
> 
> 3. **缺少 Editor UI 映射**：原文只关注了引擎内部数据结构（ECS、TypeRegistry、Tick），没有说明"AI 状态如何被编辑器可视化"。Claude Code 的 Ink 证明：没有渲染链路的 AI 集成是不完整的。

---

## 从源码到直觉：一句话总结

读了这些源码之后，我终于明白为什么 Claude Code 的界面能像原生应用一样流畅了——因为它在终端里**完整实现了一个 React 渲染引擎**；而 Kimi CLI 选择了另一条路：把 AI 的内部状态打包成**结构化的 DisplayBlock**，让客户端自己决定怎么画。对于我的游戏引擎，Kimi 的"服务端生成事件流 + 客户端自由渲染"模式更有借鉴价值。

---

## 延伸阅读与待办

- [ ] [[Kimi-CLI-DisplayBlock-与-UI-协议解析]] —— 深入 Kimi 的 Wire 渲染事件
- [ ] [[引擎-AI-状态可视化协议设计]] —— 把 DisplayBlock 思想落地到引擎 Editor
- [ ] [[引擎-AI-面板与交互设计草案]] —— Editor 内嵌 AI 面板的交互设计
- [ ] 修改 `Notes/SelfGameEngine/从零开始的引擎骨架` 中的 AgentBridge 事件流设计

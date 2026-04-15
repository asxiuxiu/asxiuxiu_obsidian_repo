---
title: MCP与Agent桥接层
date: 2026-04-15
tags:
  - self-game-engine
  - agent
  - mcp
  - ai-friendly
  - architecture
aliases:
  - AgentBridge
  - MCP适配层
---

> [← 返回 SelfGameEngine 索引]([[索引]])

# MCP 与 Agent 桥接层

> **前置依赖**：[[从零开始的引擎骨架]]、[[反射系统]]、[[系统调度与确定性]]
> **本模块增量**：引擎对外暴露标准 MCP 工具接口，AI Agent 能安全地观察世界状态、修改组件字段、推进帧、保存/回滚快照。
> **下一步**：[[多Agent编排与沙箱]] — 当需要多个专项 Agent 并行工作时，权限、隔离和冲突检测必不可少。

---

## Why：为什么引擎需要 Agent 桥接层？

### 没有这个系统时，世界有多糟

在 [[从零开始的引擎骨架]] 中，你已经搭建了一个可运行的 ECS 引擎。但如果你想让 AI 帮你开发、调试、迭代这个引擎，还有一个根本问题没解决：**AI 怎么安全地操作引擎？**

1. **AI 直接生成代码操作内存？不可控**
   - 让 AI 直接写 C++ 代码来修改 `world.positions[e].x` 不仅低效，而且极度危险。一次指针越界就可能让整个引擎崩溃。

2. **绑定特定 AI 客户端？无法扩展**
   - 如果你的引擎只支持 Claude Desktop 的插件协议，明天换成 Kimi CLI 或自研 Agent 就要重写整套集成。你需要一个**与客户端无关的标准协议**。

3. **AI 不知道能做什么、不能做什么**
   - 引擎有数百个内部函数，AI 没有边界感。它可能误删核心组件、修改受保护的运行时字段、或在迭代中触发迭代器失效。

### 三个真实场景

| 场景 | 没有桥接层的困难 | AgentBridge 的答案 |
|------|-----------------|-------------------|
| **场景 1：AI 想批量调整所有敌人的血量上限** | AI 直接写代码操作内存，可能改错偏移量或触发段错误 | 通过 `set_component_field` 结构化工具，基于反射元数据安全修改 |
| **场景 2：换了一个 AI 客户端，引擎集成要重写** | 协议绑定到特定客户端的私有 API | 统一 MCP 协议，任何支持 MCP 的客户端都能直接调用 |
| **场景 3：AI 需要知道"这个世界里有什么"** | 只能读取日志或 dump 内存 | 通过 `query_entities` 和 `get_component` 返回结构化 JSON |

### 这是"必须"还是"优化"

对于 **vibe coding**（让 AI 长时间自主迭代引擎），Agent 桥接层是**必须**：

- AI 需要"安全的操作边界" → 只有结构化的工具接口才能做到。
- AI 需要"不绑定特定客户端" → 只有 MCP 这类开放标准才能满足。
- AI 需要"知道什么能做、什么不能" → 只有 [[多Agent编排与沙箱]] 中的权限引擎和白名单才能约束。

> **核心结论**：AgentBridge 不是"锦上添花"，而是让引擎从"人类专用工具"进化为"AI 可协作伙伴"的关键基础设施。

---

## What：最简化版本的 AgentBridge 长什么样？

下面的代码是一个**真正可以编译运行**的极简 AgentBridge。相比 [[从零开始的引擎骨架]] 里那个纯内部的 `ToolRegistry`，它增加了四个关键能力：

1. **结构化显示与事件流反馈**：把引擎内部状态转化为 AI 和用户都能理解的结构化输出
2. **MCP 适配层**：引擎对外的唯一接口是标准 MCP 协议
3. **工具自描述**：AI 通过 `list_tools()` 就能知道每个工具的参数约束
4. **ENGINE_AI.md 静态上下文预加载**：让 AI 在对话开始前就"懂"引擎结构

```cpp
#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <functional>
#include <iostream>

// ============================================================
// 前置：假设这些已在 ECS 骨架和反射系统中定义
// ============================================================
using Entity = uint32_t;
struct World;
struct TypeRegistry;

// ============================================================
// 1. 结构化显示块与事件流
// ============================================================
struct EngineDisplayBlock {
    std::string type;    // "changelog", "diff", "profile", "task_status"
    std::string title;
    std::string payload; // JSON 格式详细内容
};

enum class AgentEventType {
    TurnBegin, StepBegin, ToolCall, ToolResult, DisplayBlock, StepEnd, TurnEnd
};

struct AgentEvent {
    AgentEventType type;
    std::string jsonPayload;
};

struct ToolResult {
    bool ok;
    std::string json;
    std::vector<EngineDisplayBlock> blocks;
};

// ============================================================
// 2. 工具自描述元数据
// ============================================================
struct ToolDesc {
    std::string name;
    std::string description;
    std::string inputSchema; // JSON Schema 字符串
    bool isReadOnly;
    bool isConcurrencySafe;
    bool isDestructive;
    std::function<ToolResult(const std::string& json_args)> call;
};

class ToolRegistry {
public:
    std::unordered_map<std::string, ToolDesc> tools;
    void register_tool(ToolDesc desc) { tools[desc.name] = std::move(desc); }
};

// ============================================================
// 3. AgentBridge：AI 与引擎的统一边界
// ============================================================
class AgentBridge {
    World& world;
    TypeRegistry& registry;
    ToolRegistry& toolRegistry;
    std::vector<AgentEvent> eventBuffer;
public:
    AgentBridge(World& w, TypeRegistry& r, ToolRegistry& t)
        : world(w), registry(r), toolRegistry(t) {}

    void emitEvent(AgentEventType type, const std::string& payload = "{}") {
        eventBuffer.push_back({type, payload});
    }

    std::vector<AgentEvent> consumeEvents() {
        auto out = std::move(eventBuffer);
        eventBuffer.clear();
        return out;
    }

    ToolResult execute_tool(const std::string& name, const std::string& argsJson) {
        auto it = toolRegistry.tools.find(name);
        if (it == toolRegistry.tools.end())
            return {false, R"({"error":"unknown tool"})", {}};
        return it->second.call(argsJson);
    }
};

// ============================================================
// 4. MCP 适配层
// ============================================================
class McpAdapter {
    AgentBridge& bridge;
public:
    explicit McpAdapter(AgentBridge& b) : bridge(b) {}

    std::string list_tools() {
        std::string json = "{\"tools\":[";
        bool first = true;
        for (const auto& [name, desc] : bridge.toolRegistry.tools) {
            if (!first) json += ",";
            first = false;
            json += "{\"name\":\"" + name + "\",";
            json += "\"description\":\"" + desc.description + "\",";
            json += "\"inputSchema\":" + desc.inputSchema + "}";
        }
        json += "]}";
        return json;
    }

    ToolResult call_tool(const std::string& name, const std::string& argsJson) {
        return bridge.execute_tool(name, argsJson);
    }
};
```

### 这个最小实现已经解决了什么

| 能力 | 说明 |
|------|------|
| **结构化反馈** | 工具返回 `json` 给 AI 消费，`blocks` 给 UI 渲染，一举两得 |
| **事件流可追踪** | 每次工具调用都产生 `AgentEvent`，便于审计和调试 |
| **MCP 标准协议** | 不绑定任何特定 AI 客户端，Claude / Kimi / OpenAI 通用 |
| **工具自描述** | AI 通过 `list_tools()` 就能知道每个工具的参数约束和安全标记 |

### 这个最小实现还缺什么

- **没有权限引擎**：任何 AI 都能调用任何工具（见 [[多Agent编排与沙箱]]）
- **没有沙箱隔离**：AI 的修改直接生效，无法"先试试，不行再回滚"（见 [[系统调度与确定性]] 中的 Snapshot）
- **没有多 Agent Orchestrator**：多个 AI 同时工作时会冲突（见 [[多Agent编排与沙箱]]）
- **没有 ChangeLog 集成**：AI 看不到"自上次以来世界变了什么"（见 [[系统调度与确定性]]）

---

## How：真实引擎的 Agent 桥接层是如何一步一步复杂起来的？

### 阶段 1：能用 —— 让 AI 能看见状态、能调用工具

#### 触发原因

AI 已经能接入引擎了，但需要通过结构化的方式查询和修改世界状态。

#### 代码层面的变化

**A. 从 `ToolRegistry` 自动生成默认工具**

引擎自动从 `TypeRegistry` 生成 `get_component` / `set_component` 工具：

```cpp
void register_default_tools(AgentBridge& bridge, const TypeRegistry& registry) {
    for (const auto& [compName, desc] : registry.components) {
        bridge.toolRegistry.register_tool({
            .name = "get_" + compName,
            .description = "Get " + compName + " of an entity",
            .inputSchema = R"({"type":"object","properties":{"entity":{"type":"integer"}}})",
            .isReadOnly = true,
            .call = [&](const std::string& args) -> ToolResult {
                return {true, "{...}", {}};
            }
        });
    }
}
```

**B. 标准工具集**

| 工具名 | 作用 |
|--------|------|
| `query_entities(mask)` | 按组件掩码查询实体 |
| `get_component(e, name)` | 读取某个实体的某个组件字段（JSON 返回）|
| `set_component(e, name, json)` | 修改某个实体的某个组件字段 |
| `step(dt)` | 确定性推进一帧 |
| `save_snapshot(name)` | 保存世界快照 |
| `rollback_to_snapshot(name)` | 回滚到指定快照 |

**C. ENGINE_AI.md 静态上下文预加载**

借鉴 Claude Code 的 `CLAUDE.md` 和 Prompt Caching 思想：

```cpp
void rebuild_engine_ai_md(const TypeRegistry& registry) {
    std::string md = "# Engine AI Context\n\n";
    md += "## ECS Architecture\n";
    md += "- All game state is stored in flat ECS components\n";
    md += "- Systems run in deterministic order every tick\n\n";
    md += "## Available Components\n";
    for (const auto& [name, desc] : registry.components) {
        md += "- " + name + ": ";
        for (const auto& f : desc.fields) md += f.name + " ";
        md += "\n";
    }
    md += "\n## Common Patterns\n";
    md += "- Physics tuning: start with gravity and friction\n";
    md += "- Use query_entities(mask) before bulk modifications\n";
    engine_ai_md = { md, compute_hash(md) };
}
```

- `ENGINE_AI.md` 作为 system prompt 的一部分，在每轮对话开始时注入
- 只有在 `TypeRegistry` 或工具集发生重大变化时才重新生成
- 这部分内容是"静态前缀"，可以最大化复用 Prompt Caching 的收益

### 阶段 2：好用 —— 与编辑器共用 Command 基础设施

#### 触发原因

人类编辑器和 AI Agent 需要共用同一套"修改世界"的协议，否则就要维护两套逻辑。

#### 代码层面的变化

**A. Command Pattern 就是 MCP 接口**

人类的 Undo/Redo 栈和 AI 的 `mutate` 工具本质上是一回事。设计好 `ICommand` 接口，一举两得：

```cpp
struct ICommand {
    virtual void execute() = 0;
    virtual void undo() = 0;
};
```

AI 的 `set_component` 工具内部就是构造并执行一个 `SetComponentFieldCommand`。

**B. 数据桥协议**

在 C++ 和 UI 之间建立一层结构化数据桥：
- 人类用 ImGui 滑块操作它
- AI 用 JSON 操作它
- 两者最终都转换为 `CommandBuffer` 中的命令

### 阶段 3：工业级 —— 完整 MCP Server 部署

#### 触发原因

当引擎需要被外部 IDE、自动化测试脚本或 Web 前端调用时，AgentBridge 需要成为一个独立的网络服务。

#### 代码层面的变化

**A. MCP over SSE / WebSocket**

标准 MCP 协议支持多种传输层：
- `stdio`：本地进程通信（如 Claude Desktop 插件）
- `SSE`：Server-Sent Events，适合 Web 前端
- `WebSocket`：双向实时通信

引擎的 `McpAdapter` 只需要实现标准的 JSON-RPC 层，传输层可以按需切换。

**B. 工具发现与动态注册**

引擎在运行时加载新模块时，模块可以动态向 `ToolRegistry` 注册新工具。AI 客户端通过 `list_tools()` 实时获取最新工具列表，无需重启。

---

## AI 友好设计检查清单

| 检查项 | 本模块的实现 | 说明 |
|--------|-------------|------|
| **状态平铺** | ✅ 通过 ECS 组件操作 | AgentBridge 只操作 ECS 组件字段，不直接碰内存 |
| **自描述** | ✅ ToolRegistry + ENGINE_AI.md | AI 无需头文件即可知道工具列表和参数约束 |
| **确定性** | ✅ 所有修改走 CommandBuffer | 在 Tick 边界统一应用，时序可复现 |
| **工具边界** | ✅ 结构化 JSON/Schema | 每个工具都有 `inputSchema` 和 JSON 返回值 |
| **Agent 安全** | ⚠️ 基础保护 | 核心安全机制在 [[多Agent编排与沙箱]] 中实现 |

---

## 设计权衡表

| 决策点 | 原型阶段 | 工业级阶段 |
|--------|---------|-----------|
| AI 接口 | `AgentBridge` 手写 3~5 个工具 | 完整 MCP 协议 + Schema 验证 + 动态注册 |
| 传输层 | stdio（本地进程） | SSE / WebSocket / gRPC |
| 错误恢复 | 手动调试 | 快照 + 确定性回放 + 事务沙箱 |

---

## 如果我要 vibe coding，该偷哪几招？

1. **暴露 `query_entities` + `get_component` 工具**
   - 让 AI 能用一句话拿到它关心的所有实体，并读取/修改具体字段。

2. **为 AI 设计 `EngineDisplayBlock` 反馈协议**
   - 把工具执行结果拆成两部分：`json` 给 AI 消费，`blocks` 给 UI 渲染。

3. **预生成 `ENGINE_AI.md` 作为静态上下文**
   - 把 ECS 架构、组件说明、常用调参模式写成文档，作为 system prompt 的固定前缀注入。

4. **优先实现 MCP 协议而非自定义协议**
   - 引擎的 AI 接口应该首先是一套 MCP Server，`AgentBridge` 是引擎内部对这套 MCP 接口的封装。

5. **Command Pattern 就是 AI 的 MCP 接口**
   - 人类的 Undo/Redo 栈和 AI 的 `mutate` 工具本质上是一回事。设计好 Command 接口，一举两得。

---

## 延伸阅读

- [[AI友好的引擎架构]] — 本知识库的设计宪法
- [[从零开始的引擎骨架]] — AgentBridge 依赖的 ECS 基础
- [[反射系统]] — ToolRegistry 自动生成组件工具的前提
- [[系统调度与确定性]] — CommandBuffer、Snapshot、确定性回放的实现细节
- [[多Agent编排与沙箱]] — 权限引擎、Orchestrator、冲突检测
- [[编辑器框架]] — 人类操作与 AI 操作共享同一套 Command 基础设施

> **下一步预告**
>
> 现在你的引擎已经具备单个 AI Agent 的协作能力：
> - ✅ ECS 数据模型
> - ✅ 运行时反射
> - ✅ 确定性调度
> - ✅ MCP 标准接口
>
> 下一步：[[多Agent编排与沙箱]] — 当需要多个专项 Agent 并行工作时，权限、隔离和冲突检测必不可少。

> [← 返回 SelfGameEngine 索引]([[索引]])

---
title: 多Agent编排与沙箱
date: 2026-04-15
tags:
  - self-game-engine
  - agent
  - orchestrator
  - sandbox
  - ai-friendly
aliases:
  - Multi-Agent Orchestration
---

> [← 返回 SelfGameEngine 索引]([[索引|SelfGameEngine 索引]])

# 多Agent编排与沙箱

> **前置依赖**：[[MCP与Agent桥接层]]、[[系统调度与确定性]]
> **本模块增量**：让多个专项 AI Agent 能安全并行地工作，互不破坏。引入权限引擎、事务隔离和冲突检测。
> **下一步**：[[网络同步全链路]] 或 [[内存管理全链路]] — 根据项目需求选择纵深方向。

---

## Why：为什么引擎需要多 Agent 编排与沙箱？

### 没有这个系统时，世界有多糟

在 [[MCP与Agent桥接层]] 中，你已经能让单个 AI Agent 安全地观察世界、修改组件。但真实开发场景中，**一个 Agent 远远不够**：

1. **一个 Agent 调渲染、一个 Agent 写 gameplay**
   - 它们同时修改同一个世界。如果没有隔离，渲染 Agent 可能把 gameplay Agent 刚调整的血量数值覆盖掉。

2. **AI 的改动经常引发副作用**
   - AI 调整物理参数时误删了一个核心组件；AI 修改材质时触发了 GPU 资源泄漏。没有权限控制和回滚机制，人类只能手动修 bug。

3. **上下文爆炸**
   - 单个 Agent 同时负责"改代码、调参数、跑测试、写文档"，它的上下文窗口会迅速耗尽。需要把任务拆给多个专项 Agent。

### 三个真实场景

| 场景 | 没有编排沙箱的困难 | 编排+沙箱的答案 |
|------|-----------------|-------------------|
| **场景 1：物理 Agent 和 Gameplay Agent 同时工作** | 修改互相覆盖，结果不可预测 | Orchestrator 检测组件冲突，读写重叠时串行化执行 |
| **场景 2：AI 误改核心状态导致崩溃** | 没有边界感，AI 可能删除根实体或修改引擎内部句柄 | PermissionEngine + SandboxConfig + DenialTracker 多层保护 |
| **场景 3：AI 改动后想回退** | 改完就生效，无法"先试试，不行再撤销" | CommandBuffer 事务边界 + Snapshot 自动 checkpoint |

### 这是"必须"还是"优化"

对于 **vibe coding**（让 AI 长时间自主迭代引擎）和 **多 Agent 协作开发**，多 Agent 编排与沙箱是**必须**：

- AI 需要"安全的试错空间" → 只有沙箱和快照才能做到。
- 多 Agent 需要"并行而不冲突" → 只有 Orchestrator 和组件白名单才能调度。
- 人类需要"对 AI 改动的最终控制权" → 只有 ApprovalRuntime 才能提供。

> **核心结论**：多 Agent 编排不是"高级功能"，而是让 AI 从"玩具"升级为"可信赖协作者"的安全基础设施。

---

## What：最简化版本的多 Agent 沙箱长什么样？

下面的代码建立在 [[MCP与Agent桥接层]] 和 [[系统调度与确定性]] 的基础上。它包含三个核心能力：

1. **权限引擎（PermissionEngine）**：四级规则（Deny / Ask / Allow），带拒绝追踪
2. **沙箱配置（SandboxConfig）**：组件级读写白名单
3. **事务边界**：写操作前自动 checkpoint，失败时回滚

```cpp
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

// ============================================================
// 1. 权限引擎与审批运行时
// ============================================================
enum class PermissionBehavior { Deny, Ask, Allow };

struct PermissionResult {
    PermissionBehavior behavior;
    std::string reason;
    std::string ruleSource;
};

struct DenialTracker {
    int consecutiveDenials = 0;
    int totalDenials = 0;
    static constexpr int MAX_CONSECUTIVE = 3;
    static constexpr int MAX_TOTAL = 20;

    bool shouldFallbackToPrompting() const {
        return consecutiveDenials >= MAX_CONSECUTIVE || totalDenials >= MAX_TOTAL;
    }
    void recordDenial() { consecutiveDenials++; totalDenials++; }
    void recordAllow() { consecutiveDenials = 0; }
};

struct PermissionRule {
    std::string toolName;
    std::string pattern;
    PermissionBehavior behavior;
    std::string source;
};

class PermissionEngine {
    std::vector<PermissionRule> rules;
public:
    PermissionResult evaluate(const std::string& toolName, const std::string& input) const {
        for (const auto& r : rules) {
            if (r.behavior == PermissionBehavior::Deny && matches(r, toolName, input))
                return {PermissionBehavior::Deny, "matched deny rule", r.source};
        }
        for (const auto& r : rules) {
            if (r.behavior == PermissionBehavior::Ask && matches(r, toolName, input))
                return {PermissionBehavior::Ask, "matched ask rule", r.source};
        }
        for (const auto& r : rules) {
            if (r.behavior == PermissionBehavior::Allow && matches(r, toolName, input))
                return {PermissionBehavior::Allow, "matched allow rule", r.source};
        }
        return {PermissionBehavior::Ask, "no rule matched", "default"};
    }
    bool matches(const PermissionRule& r, const std::string& toolName,
                 const std::string& input) const {
        // 简化：前缀匹配
        return toolName.find(r.toolName) == 0;
    }
};

class ApprovalRuntime {
    DenialTracker tracker;
    PermissionEngine engine;
public:
    PermissionResult resolve(const std::string& toolName, const std::string& input) {
        auto result = engine.evaluate(toolName, input);
        if (result.behavior == PermissionBehavior::Deny) {
            tracker.recordDenial();
            return result;
        }
        if (tracker.shouldFallbackToPrompting()) {
            return {PermissionBehavior::Ask, "denial limit exceeded", "tracker"};
        }
        if (result.behavior == PermissionBehavior::Allow) {
            tracker.recordAllow();
        }
        return result;
    }
};

// ============================================================
// 2. 沙箱配置
// ============================================================
struct SandboxConfig {
    std::vector<std::string> allowWrite;
    std::vector<std::string> denyWrite;
    std::vector<std::string> allowRead;
};

// ============================================================
// 3. Orchestrator：多 Agent 任务调度
// ============================================================
enum class AgentTaskType { ReadOnly, WriteHeavy, Verification };

struct AgentTask {
    std::string agentId;
    AgentTaskType type;
    std::unordered_set<std::string> targetComponents;
    std::unordered_set<uint32_t> targetEntities;
    bool reuseContext = false;
};

class AgentOrchestrator {
    std::vector<AgentTask> pendingTasks;
public:
    void submit(AgentTask task) { pendingTasks.push_back(std::move(task)); }

    void dispatchAll() {
        std::vector<AgentTask> reads, writes, verifies;
        for (auto& t : pendingTasks) {
            if (t.type == AgentTaskType::ReadOnly) reads.push_back(t);
            else if (t.type == AgentTaskType::WriteHeavy) writes.push_back(t);
            else verifies.push_back(t);
        }

        // 读任务：自由并行
        for (auto& t : reads) { /* execute(t) */ }

        // 写任务：按目标组件/实体集合检测冲突，有重叠则串行
        for (size_t i = 0; i < writes.size(); ++i) {
            bool conflict = false;
            for (size_t j = 0; j < i; ++j) {
                if (hasOverlap(writes[i].targetComponents, writes[j].targetComponents) &&
                    hasOverlap(writes[i].targetEntities, writes[j].targetEntities)) {
                    conflict = true;
                    break;
                }
            }
            if (conflict) { /* wait for writes[j] to finish */ }
            /* execute(writes[i]) */
        }

        for (auto& t : verifies) { /* execute(t) */ }
        pendingTasks.clear();
    }

    bool hasOverlap(const std::unordered_set<std::string>& a,
                    const std::unordered_set<std::string>& b) const {
        for (auto& x : a) if (b.count(x)) return true;
        return false;
    }
};
```

### 这个最小实现已经解决了什么

| 能力 | 说明 |
|------|------|
| **权限分级** | Deny / Ask / Allow 四级规则，拒绝过多时自动降级为 Ask |
| **组件白名单** | 每个 Agent 只能读写被授权的组件类型 |
| **读写隔离** | 读任务自由并行，写任务按组件/实体冲突检测串行化 |
| **可追踪** | DenialTracker 记录拒绝历史，便于审计和调试 |

### 这个最小实现还缺什么

- **没有真正的两阶段提交**：写任务失败时还不能原子性回滚整个世界状态。
- **没有 CRDT 风格的冲突合并**：两个 Agent 同时修改同一字段时，只能串行化，不能自动合并。
- **没有 Agent 间通信协议**：Agent 之间无法协商或共享中间结果。
- **没有 UI 审批接口**：`Ask` 状态需要人类确认，但还没有交互层。

---

## How：真实引擎的多 Agent 编排是如何一步一步复杂起来的？

### 阶段 1：能用 —— 单个 Agent + 简单白名单

#### 触发原因

AI 已经可以接入引擎了，但需要防止它误操作核心状态。

#### 代码层面的变化

**A. 引入 SandboxConfig**

为每个 Agent 分配一个白名单：
- `PhysicsAgent`：只读写 `RigidBody`、`Collider`
- `RenderAgent`：只读写 `Material`、`Mesh`
- `GameplayAgent`：只读写 `Health`、`Inventory`

**B. 写操作前自动 checkpoint**

利用 [[系统调度与确定性]] 中的 `Snapshot`，在 AI 执行写工具前保存当前世界状态。如果 AI 的改动导致 Invariant 断言失败，立即回滚到 checkpoint。

---

### 阶段 2：好用 —— 权限引擎、事务隔离与变更追踪

#### 触发原因

AI 的改动经常引发副作用，需要权限控制、事务隔离和变更追踪。

#### 代码层面的变化

**A. ChangeLog 对 AI 的特殊价值**

纯引擎的 `ChangeLog` 已经用于调试。对 AI 来说，它是**避免上下文爆炸**的关键：
- AI 不需要读整个世界，只需要读这一帧的 `changelog`。

配合**滚动窗口清理**，超过 16 帧的旧记录折叠为统计，防止上下文无限膨胀。

**B. CommandBuffer 作为 AI 的写缓冲**

AI 的改动先进入 `CommandBuffer`，在 Tick 边界统一应用。如果权限检查或冲突检测失败，可以整批回滚。这与编辑器操作、网络同步共用同一套基础设施（详见 [[系统调度与确定性]]）。

**C. 权限控制与安全边界**

引入四级规则来源、拒绝追踪、工具级参数检查：

```cpp
struct ToolPermissionChecker {
    virtual PermissionResult check(const std::string& inputJson) const = 0;
};
```

- **系统级规则**：引擎硬编码的不可变规则（如禁止删除 `World` 根实体）。
- **项目级规则**：`.agent_rules` 文件中的项目特定约束（如禁止修改 `Player` 的 `id` 字段）。
- **会话级规则**：当前对话中临时生效的约束（如"本次只调渲染参数"）。
- **工具级检查**：针对某个工具的参数校验（如 `set_component` 检查字段是否在白名单内）。

---

### 阶段 3：工业级 —— 让多 Agent 能安全并行

#### 触发原因

一个 AI 调渲染、一个 AI 写 gameplay、一个 AI 优化物理。它们需要同时工作，但不能互相破坏。

#### 代码层面的变化

**A. Orchestrator 层调度多 Agent**

```cpp
// PhysicsAgent：只读写 RigidBody、Collider
// RenderAgent：只读写 Material、Mesh
// GameplayAgent：只读写 Health、Inventory
// Orchestrator 负责合并改动、检测冲突、决定执行顺序。
```

> 借鉴 Claude Code Coordinator 的并发哲学：**读任务自由并行，写任务按组件集/实体集串行化**。

**B. 确定性 Replay Pipeline 的 AI 测试价值**

纯引擎的确定性回放已经用于网络同步（见 [[系统调度与确定性]]）。对 AI 来说，它是**自主跑回归测试**的基石：
- AI 可以改动参数 → 重放同一批输入 → 验证输出是否一致。

**C. 核心系统插入 Invariant 断言**

让引擎本身成为 Agent 改动的"自动审核员"：
- `TransformSystem` 断言：不存在循环父子关系。
- `PhysicsSystem` 断言：质量必须大于 0。
- `ResourceManager` 断言：引用计数不能为负。

如果 AI 的改动违反了任何 Invariant，命令缓冲在应用前就会被拒绝。

---

## AI 友好设计检查清单

| 检查项 | 本模块的实现 |
|--------|-------------|
| **状态平铺** | ✅ Agent 只操作 ECS 组件字段，不直接碰内存 |
| **自描述** | ✅ 白名单、规则表、任务类型都是结构化数据，可被 AI 查询 |
| **确定性** | ✅ 所有修改走 CommandBuffer，在 Tick 边界统一应用，时序可复现 |
| **工具边界** | ✅ 结构化 JSON/Schema，每个 Agent 的权限边界清晰 |
| **Agent 安全** | ✅ 四层保护：PermissionEngine + SandboxConfig + CommandBuffer + Snapshot |

---

## 设计权衡表

| 决策点 | 原型阶段 | 工业级阶段 |
|--------|---------|-----------|
| 权限模型 | 简单白名单 | 四级规则来源 + 工具级参数检查 + 审批运行时 |
| 并发模型 | 单 Agent 顺序调用 | Orchestrator + 读写隔离 + 冲突检测 |
| 错误恢复 | 手动调试 | 快照 + 确定性回放 + 事务沙箱 |
| 冲突处理 | 直接串行化 | 串行化 + 可选 CRDT 合并 |

---

## 如果我要 vibe coding，该偷哪几招？

1. **从简单白名单开始**
   - 不要一上来就写完整的 PermissionEngine。先给每个 Agent 一个 `allowWrite` 列表，足够用很久。

2. **ChangeLog 实施滚动窗口清理**
   - 超过 16 帧的旧记录自动折叠为统计，防止 AI 上下文无限膨胀。

3. **写操作前自动 checkpoint**
   - 利用已有的 `Snapshot` 能力，在 AI 执行任何非只读工具前保存世界状态。这是"安全试错"的最低成本实现。

4. **引入 Orchestrator 层调度多个专项 Agent**
   - 每个 Agent 绑定组件白名单，防止"越权操作"。

5. **核心系统插入 Invariant 断言**
   - 让引擎本身成为 Agent 改动的"自动审核员"。这比在外部写一万条规则更可靠。

---

## 延伸阅读

- [[AI友好的引擎架构]] — 本知识库的设计宪法
- [[MCP与Agent桥接层]] — AgentBridge 和 MCP 适配的核心实现
- [[系统调度与确定性]] — CommandBuffer、Snapshot、确定性回放的实现细节
- [[编辑器框架]] — 人类操作与 AI 操作共享同一套 Command 基础设施

---

> **下一步预告**
>
> 现在你的引擎已经具备完整的 AI 协作能力：
> - ✅ ECS 数据模型
> - ✅ 运行时反射
> - ✅ 确定性调度
> - ✅ MCP 标准接口
> - ✅ 多 Agent 编排与沙箱
>
> 下一步可以进入工业级专题增强：[[网络同步全链路]]、[[内存管理全链路]] 或 [[构建系统与CI-CD]]。

> [← 返回 SelfGameEngine 索引]([[索引|SelfGameEngine 索引]])

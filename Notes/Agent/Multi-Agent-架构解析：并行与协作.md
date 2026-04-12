---
title: Multi-Agent 架构解析：并行与协作
description: 从 Coordinator 到 Swarm，理解 Claude Code 与 Kimi CLI 如何让多个 Agent 分工协作而不互相踩脚
tags:
  - agent
  - source-analysis
  - multi-agent
---

> [← 返回 Agent 索引]([[Agent/索引|Agent 索引]])

# Multi-Agent 架构解析：并行与协作

## Why：为什么要理解 Multi-Agent？

### 问题背景

当一个任务足够复杂时，单个 Agent 的上下文窗口、专注领域和执行能力都会遇到瓶颈。想象你要重构一个大型项目的认证系统：
- 需要有人先**调研**现有代码
- 需要有人**设计**新接口
- 需要有人**实现**改动
- 需要有人**验证**测试通过

如果全部交给一个 Agent，它要么在上下文中塞满无关信息导致"失忆"，要么在实现到一半时忘记最初的设计约束。

在游戏引擎里，这个问题会以更尖锐的形式出现：
- `PhysicsAgent` 正在调整碰撞参数
- `GameplayAgent` 同时修改敌人血量
- 两个 Agent 的改动互相覆盖，最终谁也说不清世界状态是怎么变成这样的。

### 不用 Multi-Agent 架构的后果

1. **单 Agent 硬扛所有任务**：上下文爆炸、频繁失忆、执行深度不够
2. **多 Agent 盲目并行**：没有隔离边界，写操作互相冲突，结果不可预测
3. **权限无法委托**：每个 Agent 都需要用户手动审批，失去了"自主"的意义
4. **结果无法汇总**：子 Agent 跑完返回一大段文本，父 Agent 无法高效地提取关键信息

### 应用场景

1. **大规模代码重构**：调研、设计、实现、验证由不同 Agent 分工，Coordinator 汇总
2. **引擎多系统并行调参**：物理、渲染、Gameplay 三个 Agent 同时工作，各自只读写授权的组件
3. **复杂 Bug 排查**：一个 Agent 读日志，一个 Agent 读源码，一个 Agent 写修复，Leader 协调信息流动

> [!tip] 从对话框出发
> 在引入 Multi-Agent 之前，AI 只能一次处理一件事。引入之后，它可以像一位项目经理一样，把大任务拆成子任务、分配给多个专家并行执行、收集结果并做最终决策。这个质变藏在"隔离 + 通信 + 调度"这三层架构里。

---

## What：Multi-Agent 的本质是什么？

- **核心定义**：Multi-Agent 不是简单地"开多个 LLM 会话"，而是一套**任务分解、执行隔离、结果聚合、权限委托**的完整协作协议。
- **关键洞察**：模型本身没有变聪明，变聪明的是**外部编排层**——它知道什么时候该并行、什么时候该串行、什么时候该汇总。

### 核心概念速查

| 概念 | 作用 | 在 Agent 中的体现 |
|------|------|------------------|
| **Coordinator** | 中心化调度者，负责拆任务、发指令、收结果 | Claude Code 的 `coordinatorMode.ts` |
| **Worker / Teammate** | 执行具体子任务的 Agent | Claude Code 的 `AgentTool` / `InProcessTeammate` |
| **Mailbox** | 基于文件系统的异步消息队列，用于 Agent 间通信 | Claude Code `utils/swarm/` 中的 `teammateMailbox` |
| **AsyncLocalStorage** | 在单进程内为不同 Agent 提供上下文隔离 | Claude Code `utils/teammateContext.ts` |
| **Subagent** | Kimi CLI 中的嵌套 Agent 概念 | `ForegroundSubagentRunner` / `BackgroundAgentRunner` |
| **Soul** | Kimi CLI 中一个可运行的 Agent 实例（含 LLM + 上下文） | `KimiSoul` |
| **Summary Continuation** | 子 Agent 返回结果太短时的自动补全机制 | `run_with_summary_continuation` |
| **Tool Policy** | 子 Agent 可使用的工具白名单/继承策略 | `ToolPolicy(mode="inherit" \| "allowlist")` |

> [!info] 概念插播：AsyncLocalStorage
> 
> **直觉版**：想象一个大型开放式办公室，每个员工（Agent）都有一张带隐私屏风的办公桌。屏风里的文件只有该员工能看到，换到另一张桌子就看不到了。
> 
> **定义版**：`AsyncLocalStorage` 是 Node.js 提供的一种异步上下文存储机制。它让你在同一个进程内的不同异步调用链之间，存储和读取各自独立的数据，而不会互相串台。
> 
> **为什么 Claude Code 要用它**：当多个 Worker Agent 在同一个进程内并行运行时，Claude Code 需要知道"当前这个工具调用是由哪个 Agent 发起的"。`AsyncLocalStorage` 就像给每个 Agent 发了一张独立的"工作证"，代码在任何地方都能查到"现在是谁在干活"，从而把权限、日志、状态正确归属到对应的 Agent。

---

---

前面我们搞懂了 **Multi-Agent 的本质不是"开多个 LLM 会话"，而是一套任务分解、执行隔离、结果聚合、权限委托的完整协作协议**。现在我们要回答的问题是：**Claude Code 和 Kimi CLI 分别是怎样让多个 Agent 协作的？** Claude 像一位项目经理，带着一群专业工人分工干活；Kimi 像一位调酒师，把不同的基酒（子 Agent）按配方调在一起。接下来我们把源码拆开，一层层看。

---

## How：不同 Agent 工具是如何实现的？

### 1. 宏观对比：Claude Code vs Kimi CLI

**先给一个总体的直觉比喻**：

> 想象你要装修一套房子。Claude Code 的做法是请一位项目经理（Coordinator），他先派 3 个工人同时去量房（并行调研），然后自己画设计图（Synthesis），再派泥瓦匠和电工按图纸施工（串行或并行实施），最后请监理验收（Verification）。Kimi CLI 的做法是像调鸡尾酒：需要某种味道时，就倒入对应的基酒（Subagent），调完一杯立刻倒进 Parent Agent 的大杯子里。

接下来再展开细节对比：

| 维度 | Claude Code | Kimi CLI | 差异原因分析 |
|------|-------------|----------|-------------|
| **核心抽象** | 团队模式：项目经理（Coordinator）+ 一群工人（Worker/Teammate） | 父子模式：父 Agent + 子 Agent（Subagent，可前台或后台运行） | Claude 产品定位更偏向"团队协作者"；Kimi 更偏向"嵌套工具调用" |
| **通信方式** | 工人之间通过"邮箱"（Mailbox，基于文件系统）传递消息，像贴便利贴 | 父子之间通过 Wire 协议事件流 + 工具返回值直接传递，像面对面交接 | Claude 的工人可能不在同一个房间（跨进程/tmux），需要用文件系统持久化通信；Kimi 的子 Agent 通常在同一个进程内，可以直接对话 |
| **隔离级别** | 三种隔离可选：同一张桌子（InProcess）、隔壁房间（tmux）、另一栋楼（Worktree） | 主要靠同进程内的上下文拷贝隔离，像在同一间办公室里用屏风隔开 | Claude 需要支持长期存活、可被独立查看的 teammate；Kimi 更强调轻量、快速的一次性任务 |
| **并发调度** | 项目经理的系统提示里明确规定："读任务自由并行，写任务按文件集串行" | 主要依靠前台/后台 Runner 管理单次运行，并发由父 Agent 自己决定 | Claude 把"并行"写进了 AI 的行为准则；Kimi 把并发控制权交给调用方 |
| **错误恢复** | 项目经理决定"让同一个人继续干"（Continue）还是"换新人重来"（Spawn Fresh） | 如果子 Agent 结果太短，自动追加 prompt 要求补充（Summary Continuation） | Claude 强调工作流阶段和人机协作感；Kimi 强调自动化和结果完整性 |
| **权限委托** | 项目经理把自己的权限"复印"给工人，但可以通过文件系统同步撤销 | 为每个子 Agent 创建独立的审批源（ApprovalSource），像给每张信用卡设单独的额度 | 两者都有审批运行时，但 Claude 额外有 Swarm 专用的权限同步文件系统 |
| **对引擎的启示** | 如果你要做**复杂关卡搭建**（需要多人长期协作），学 Claude 的 Director + Worker + 事务沙箱 | 如果你要做**快速参数扫描**（一次性任务，结果汇总），学 Kimi 的轻量 Subagent | 引擎可以融合两者：复杂任务用 Coordinator，快速任务用 Subagent |

> **用人话讲**：Claude Code 的 Multi-Agent 更像一个真实的工程团队——有项目经理、有分工、有会议（Mailbox）、有监理（Verification）。而 Kimi CLI 的 Subagent 更像"函数调用"——父 Agent 需要某个专业能力时，就调用一个子 Agent 函数，拿到结果后继续执行。前者适合"协作"，后者适合"外包"。

> [!info] 概念插播：Continue vs Spawn Fresh
> 
> **直觉版**：想象你是一个项目经理。Continue 就是"让同一个人继续做下去"，他记得之前踩过的坑；Spawn Fresh 就是"换一个新员工来做"，他没有之前的偏见，但也少了上下文。
>
> **定义版**：Claude Code 的 Coordinator 在收到 Worker 结果后，必须决定是用 `SendMessageTool` 继续同一个 Worker，还是用 `AgentTool` 启动一个新 Worker。
>
> **为什么这里要用它**：Worker 的上下文里有它之前读过的文件、试过的方案。如果下一步任务和这些高度重叠（比如调研后直接在同文件上改代码），Continue 更优；如果下一步需要" fresh eyes "（比如独立验证），Spawn Fresh 更优。
>
> **引擎审视**：`Notes/SelfGameEngine/从零开始的引擎骨架` 中 **AgentOrchestrator** 只提到"按权限隔离并行调度 Agent"，但没有说明**同一个 Agent 实例的上下文复用**问题。根据 Claude Code 的实现，引擎的 Orchestrator 应该支持：
> - **Stateful Agent**：可以持续对话、复用世界观察上下文
> - **Stateless Agent**：每次调用都是全新上下文，避免污染

> [!info] 概念插播：Summary Continuation
> 
> **直觉版**：你让员工写一份报告，他交了一张便利贴。你不会直接拿这张便利贴去跟 CEO 汇报，而是会说"写详细点"。
>
> **定义版**：Kimi CLI 在 `runner.py` 中定义了 `SUMMARY_MIN_LENGTH = 200`。如果子 Agent 的输出少于 200 字符，系统会自动追加一条 prompt 要求它提供更全面的技术细节和分析。
>
> **为什么这里要用它**：子 Agent 的结果会被父 Agent 消费。如果结果太简略，父 Agent 可能做不出正确的后续决策。Summary Continuation 是一道自动化的质量门。
>
> **引擎审视**：`从零开始的引擎骨架` 中没有任何关于 **Agent 返回结果质量校验** 的设计。根据 Kimi CLI 的实现，引擎的 `AgentBridge` 应该增加：
> - **输出长度/完整性校验**：如果 Agent 查询结果太简略，自动要求补充
> - **结构化输出校验**：要求 Agent 返回的必须是有效 JSON/特定 schema

> [!warning] 如果 Claude Code 不用 Mailbox 会怎样？
> 如果 Coordinator 和 Worker 之间没有 Mailbox（文件系统通信），那么一旦 Worker 所在的进程崩溃，Coordinator 就永远收不到结果，也无法知道 Worker 是死是活。Mailbox 就像一个"寄存柜"，Worker 把结果放进去，Coordinator 随时来取，即使 Worker 已经下班了也没关系。

> [!warning] 如果 Kimi CLI 不做 Summary Continuation 会怎样？
> 如果子 Agent 返回"ok"两个字，父 Agent 可能误以为任务已经完成，但其实没有任何有用的信息被传递。这会导致父 Agent 在信息不足的情况下做出错误决策，就像 CEO 基于"业绩还行"来做战略规划一样危险。

### 2. 核心机制伪代码

#### Claude Code 的 Coordinator 工作流

这段伪代码模拟了 Claude Code 的 Coordinator 如何把一个大任务拆成调研 → 设计 → 实施 → 验证四个阶段。

```
function coordinator_loop(user_request):
    // 1. Research 阶段：并行启动多个只读 Worker
    workers = [
        spawn_worker("调研代码结构", prompt="..."),
        spawn_worker("查找相关测试", prompt="..."),
        spawn_worker("检查依赖关系", prompt="...")
    ]
    
    // 2. 等待所有结果到达（通过 mailbox/XML 通知）
    findings = collect_notifications(workers)
    
    // 3. Synthesis：Coordinator 自己理解并写出规范
    spec = synthesize(findings)
    
    // 4. Implementation：串行或按文件集并行执行
    if write_tasks_share_files(tasks):
        for task in tasks:
            result = spawn_worker_or_continue(task, spec)
    else:
        results = parallel_spawn(tasks, spec)
    
    // 5. Verification：用新 Worker 独立验证
    verifier = spawn_fresh_worker("验证改动", prompt=spec)
    return summarize_all()
```

**这段代码在做什么**：这就像一个装修项目经理的工作流程。第一步，同时派 3 个工人去量房、看水电、查材料（并行调研）；第二步，项目经理自己根据所有调研结果画出设计图；第三步，判断哪些施工可以同时进行（不共享墙面的），哪些必须串行（同一面墙要先拆后砌）；第四步，请一个全新的监理来验收，确保没有"自己查自己"的偏见。

**核心设计思想**：
- **读任务自由并行**：因为读操作不会互相冲突
- **写任务按文件集串行**：如果两个写任务修改同一个文件，必须排队，否则会产生冲突
- **独立验证**：验证者必须是" fresh eyes "，不能是之前的实施者

#### Kimi CLI 的 Subagent 生命周期

这段伪代码模拟了 Kimi CLI 如何"调一杯鸡尾酒"——准备子 Agent、恢复上下文、运行、补齐结果。

```python
function run_subagent(request):
    # 1. 准备实例（新建或恢复）
    prepared = prepare_instance(request)
    
    # 2. build-restore-prompt 流水线
    agent = builder.build_builtin_instance(agent_id, type_def, launch_spec)
    context = Context(store.context_path(agent_id))
    await context.restore()
    
    # 3. 系统提示：恢复时复用 persisted prompt，首次运行时持久化
    if context.system_prompt is not None:
        agent.system_prompt = context.system_prompt
    else:
        await context.write_system_prompt(agent.system_prompt)
    
    # 4. 运行 soul
    final_response, failure = await run_with_summary_continuation(
        soul, prompt, ui_loop_fn, wire_path
    )
    
    # 5. 如果结果太短，自动追加"请提供更全面的总结"
    if len(final_response) < SUMMARY_MIN_LENGTH:
        final_response = await run_soul_checked(
            soul, SUMMARY_CONTINUATION_PROMPT, ...
        )
    
    return final_response
```

**这段代码在做什么**：Kimi CLI 把子 Agent 的运行比作"调酒师的六步 workflow"。第一步，拿出一个干净的杯子（准备实例）；第二步，倒入基酒（构建 Agent）并检查之前调好的酒是否还在（恢复上下文）；第三步，确认配方（系统提示）；第四步，正式调制（运行 soul）；第五步，尝一口，如果味道太淡就再加点料（Summary Continuation）。

**核心设计思想**：
- **生命周期与运行解耦**：`prepare_soul` 只负责把状态恢复到"可运行"，真正调用 LLM 的是 `runner.py`
- **上下文恢复**：子 Agent 可以像游戏读档一样恢复之前的对话状态
- **结果质量门**：`SUMMARY_MIN_LENGTH` 防止子 Agent 返回过短、过含糊的结果

### 3. 关键源码印证

#### Claude Code：Coordinator 的系统提示

**这段代码在做什么**：这是 Claude Code 给 Coordinator（项目经理）写的"岗位说明书"。它明确告诉 Coordinator：你的职责是派工人调研、设计规范、监督实施、验收结果。最重要的是，它把"并行"直接写进了行为准则里。

```typescript
// D:/workspace/claude-code-main/src/coordinator/coordinatorMode.ts
return `You are Claude Code, an AI assistant that orchestrates software engineering tasks across multiple workers.

## 1. Your Role
You are a **coordinator**. Your job is to:
- Help the user achieve their goal
- Direct workers to research, implement and verify code changes
- Synthesize results and communicate with the user

## 4. Task Workflow
| Phase | Who | Purpose |
|-------|-----|---------|
| Research | Workers (parallel) | Investigate codebase, find files, understand problem |
| Synthesis | **You** (coordinator) | Read findings, understand the problem, craft implementation specs |
| Implementation | Workers | Make targeted changes per spec, commit |
| Verification | Workers | Test changes work |

### Concurrency
**Parallelism is your superpower. Workers are async. Launch independent workers concurrently whenever possible...**

Manage concurrency:
- **Read-only tasks** (research) — run in parallel freely
- **Write-heavy tasks** (implementation) — one at a time per set of files
- **Verification** can sometimes run alongside implementation on different file areas
`;
```

**为什么这样设计**：这段代码揭示了一个非常重要的洞察——**"并行"不是底层实现偷偷干的，而是被写进了 Coordinator 的 system prompt 里，成为它的显式行为准则。** 这意味着 Coordinator 的"智能"很大程度上来自于提示工程对它角色的塑造。如果不用这种设计，底层即使支持并行，Coordinator 也可能因为"保守"而串行派发任务，浪费大量时间。

#### Claude Code：InProcessTeammate 的内存上限

**这段代码在做什么**：这段注释讲述了一个真实的生产事故。某次"鲸鱼会话"中，292 个 Agent 在 2 分钟内被启动，内存飙到 36.8GB。原因是 AppState 中每个 teammate 都保存了一份消息的完整副本。解决方案是：**UI 镜像只保留最近 50 条消息**。

```typescript
// D:/workspace/claude-code-main/src/tasks/InProcessTeammateTask/types.ts
/**
 * BQ analysis (round 9, 2026-03-20) showed ~20MB RSS per agent at 500+ turn
 * sessions and ~125MB per concurrent agent in swarm bursts. Whale session
 * 9a990de8 launched 292 agents in 2 minutes and reached 36.8GB. The dominant
 * cost is this array holding a second full copy of every message.
 */
export const TEAMMATE_MESSAGES_UI_CAP = 50
```

**为什么这样设计**：多 Agent 系统的内存成本不是线性的，如果不做主动上限控制，会瞬间压垮系统。这个教训告诉我们：**不要为了让 UI 完美展示而保存完整历史**。对于引擎设计来说，运行时状态和 UI 展示状态必须分离——运行时保留完整历史用于确定性回放，UI 只展示最近 N 条。

#### Kimi CLI：Subagent 的 build-restore-prompt 流水线

**这段代码在做什么**：这是 Kimi CLI 的 `prepare_soul` 函数，一个非常清晰的六步流水线：构建 Agent → 恢复上下文 → 管理系统提示 → 注入上下文 → 写调试快照 → 实例化 Soul。

```python
# D:/workspace/kimi-cli-main/src/kimi_cli/subagents/core.py
async def prepare_soul(spec, runtime, builder, store, on_stage):
    # 1. Build agent from type definition
    agent = await builder.build_builtin_instance(...)
    
    # 2. Restore conversation context
    context = Context(store.context_path(spec.agent_id))
    await context.restore()
    
    # 3. System prompt: reuse persisted prompt on resume, persist on first run
    if context.system_prompt is not None:
        agent = replace(agent, system_prompt=context.system_prompt)
    else:
        await context.write_system_prompt(agent.system_prompt)
    
    # 4. For new (non-resumed) explore agents, prepend git context
    prompt = spec.prompt
    if spec.type_def.name == "explore" and not spec.resumed:
        git_ctx = await collect_git_context(...)
        if git_ctx:
            prompt = f"{git_ctx}\n\n{prompt}"
    
    # 5. Write prompt snapshot (debugging aid)
    store.prompt_path(spec.agent_id).write_text(prompt, encoding="utf-8")
    
    # 6. Create soul
    soul = KimiSoul(agent, context=context)
    return soul, prompt
```

**为什么这样设计**：这个六步流水线的优雅之处在于把"生命周期管理"和"运行"彻底解耦。`prepare_soul` 只负责把 Agent 状态恢复到"可运行"，而 `runner.py` 才负责真正调用 LLM。这就像赛车的后勤团队只负责把车整备到"随时可以发车"，而车手（runner）才负责踩下油门。这种解耦让代码更容易测试、更容易调试、也更容易扩展新的 Agent 类型。

#### Kimi CLI：Foreground Runner 的审批源隔离

**这段代码在做什么**：这是 Kimi CLI 为每个前台子 Agent 运行创建的独立"审批身份证"。每个子 Agent 都有自己的 `ApprovalSource`，这样父 Agent 和子 Agent 的审批请求不会混在一起。如果子 Agent 被取消了，系统可以通过 `cancel_by_source` 批量撤销它发出的所有 pending 审批。

```python
# D:/workspace/kimi-cli-main/src/kimi_cli/subagents/runner.py
approval_source = ApprovalSource(
    kind="foreground_turn",
    id=uuid.uuid4().hex,
    agent_id=agent_id,
    subagent_type=actual_type,
)
approval_source_token = set_current_approval_source(approval_source)

try:
    final_response, failure = await run_with_summary_continuation(...)
finally:
    if approval_source_token is not None:
        reset_current_approval_source(approval_source_token)
    if approval_source is not None and self._runtime.approval_runtime is not None:
        self._runtime.approval_runtime.cancel_by_source(
            approval_source.kind, approval_source.id
        )
```

**为什么这样设计**：这里展示了一个精妙的权限隔离设计。如果没有这种基于作用域的审批委托，子 Agent 的权限就会和父 Agent 混在一起。想象一下：你允许父 Agent 修改文件，但子 Agent 突然发起了一个删除数据库的请求——如果没有独立的审批源，你就无法区分"这是谁发起的请求"，也就无法精确撤销或限制。

## 引擎映射：这个设计对我的游戏引擎有什么启发？

### 1. 对应系统

Multi-Agent 架构在引擎中最接近的是 **`AgentOrchestrator` + `TransactionalWorld`**：
- **Coordinator / Director** 对应 `AgentOrchestrator`：负责把高层指令（"搭建一个射击关卡"）拆成子任务，分配给不同专项 Agent
- **Worker / Subagent** 对应 `AgentBridge` 的多个连接实例：每个 Agent 有自己的权限边界和上下文状态
- **Mailbox / Wire 协议** 对应 Agent 间的通信协议：可以是共享队列、事件总线或中间表示（如关卡描述 JSON）
- **权限桥接** 对应 `AgentBridge` 的 `allowedComponents` 和工具审批运行时

### 2. 可借鉴点

#### 借鉴 1：读并行、写串行的调度策略

Claude Code 的 Coordinator 系统提示里明确规定了并发规则：
- 只读任务（如 `query_entities`、`get_component`）可以自由并行
- 写任务（如 `set_component`、`create_actor`）必须按"文件集/组件集"串行化

这可以直接迁移到引擎的 `AgentOrchestrator` 中：

```cpp
enum class TaskType { ReadOnly, WriteHeavy, Verification };

class AgentOrchestrator {
    // 读任务：直接并行派发
    // 写任务：检查目标组件/实体集合是否有重叠，有则串行，无则并行
};
```

#### 借鉴 2：Summary Continuation——子 Agent 结果不能太短

Kimi CLI 的 `SUMMARY_MIN_LENGTH = 200` 是一道结果质量门。引擎中的 Agent 工具调用也应该有类似的校验：
- 如果 `PhysicsAgent` 返回的调参报告只有"ok"两个字，Orchestrator 应该自动要求补充"改了什么参数、基于什么观察、预期效果是什么"
- 这可以避免父 Agent 在信息不足的情况下做出错误决策

#### 借鉴 3：基于作用域的审批委托

Kimi CLI 为每个 Subagent 创建独立的 `ApprovalSource`，这与引擎中"不同 Agent 的写操作需要独立审批"的需求完全匹配。引擎的审批运行时应该支持：
- 按 `agent_id` 分组审批请求
- 当某个 Agent 被中断/取消时，批量撤销它的 pending 审批
-  Leader（用户/Director）可以一键允许/拒绝某个 Agent 的所有同类操作

#### 借鉴 4：内存上限控制——"鲸鱼会话"的教训

Claude Code 的 `TEAMMATE_MESSAGES_UI_CAP = 50` 告诉我们：**多 Agent 系统的内存成本会指数级增长**。引擎中的 Agent 上下文管理必须设置硬上限：
- 每个 Agent 的上下文窗口有上限（如最多 N 轮对话）
- UI 状态镜像与真实运行状态分离，UI 只做最近 N 帧的展示
- 长期存活的 Agent 需要定期压缩或摘要化历史

### 3. 审视与修正行动项

> [!warning] 根据 Agent 源码洞察，`Notes/SelfGameEngine/从零开始的引擎骨架.md` 中有以下几处需要修正：

#### 修正 1：AgentOrchestrator 过于理想化，缺少调度策略

**现状**：原笔记中的 `AgentOrchestrator` 只有一个 `dispatch_all` 函数，提到"按权限隔离并行调度 Agent"，但没有说明：
- 读任务和写任务的不同调度策略
- 写冲突检测机制
- 同一个 Agent 实例是否支持上下文复用（Stateful）

**修正方向**：为 `AgentOrchestrator` 增加调度策略层：

```cpp
class AgentOrchestrator {
public:
    struct Task {
        std::string agentId;
        TaskType type;  // ReadOnly / Write / Verify
        std::unordered_set<std::string> targetComponents;
        std::unordered_set<Entity> targetEntities;
        std::function<void()> execute;
    };

    void dispatch(std::vector<Task> tasks);
};
```

- **ReadOnly 任务**：直接全部并行
- **Write 任务**：检查 `targetComponents × targetEntities` 是否有交集，有交集则串行，无交集则并行
- **Verify 任务**：如果验证区域与正在执行的 Write 区域不重叠，可以并行

#### 修正 2：缺少 Agent 结果质量校验机制

**现状**：原笔记假设 Agent 调用工具后返回的结果总是可用的。但 Kimi CLI 的 `SUMMARY_MIN_LENGTH` 和 `run_with_summary_continuation` 证明，**子 Agent 可能返回过短、过含糊的结果**。

**修正方向**：在 `AgentBridge` 或 `AgentOrchestrator` 中增加结果后处理层：
- 检查返回长度/字段完整性
- 如果结果不满足最低要求，自动追加补充 prompt
- 对于引擎查询工具，要求返回结构化 JSON，并做 schema 校验

#### 修正 3：权限模型缺少"按 Agent 作用域分组审批"

**现状**：原笔记中只有简单的 `allowedComponents` 白名单检查，没有提到审批运行时的作用域管理。

**修正方向**：引入 `ApprovalSource` 概念：

```cpp
struct ApprovalSource {
    std::string kind;      // e.g., "agent_turn"
    std::string id;        // uuid
    std::string agentId;   // 所属 Agent
};

class ApprovalRuntime {
public:
    void setCurrentSource(const ApprovalSource& source);
    void cancelBySource(const std::string& kind, const std::string& id);
};
```

这样当用户说"停止所有 PhysicsAgent 的操作"时，可以精确撤销该 Agent 的所有 pending 审批，而不会影响 GameplayAgent。

#### 修正 4：多 Agent 内存管理缺少上限控制

**现状**：原笔记中没有提及 Agent 上下文的内存上限或清理策略。

**修正方向**：
- 每个 Agent 的对话历史设置硬上限（如 `MAX_AGENT_HISTORY = 50`）
- UI 展示层与运行时状态层分离：运行时保留完整历史用于确定性回放，UI 只展示最近 N 条
- 引入类似于 `TEAMMATE_MESSAGES_UI_CAP` 的滚动窗口机制

---

## 从源码到直觉：一句话总结

> 读了这些源码之后，我终于明白为什么 AI 能从"一个人干所有活"进化到"像项目经理一样指挥多个专家并行作战"了——因为 **Multi-Agent 的本质不是模型变多了，而是外部套了一层"隔离 + 通信 + 调度"的协作协议**：Claude Code 用 Coordinator 的系统提示把"读并行、写串行"写进了 AI 的行为准则里，用 Mailbox 和 AsyncLocalStorage 让 teammates 既能独立存活又能共享上下文；Kimi CLI 则用 `prepare_soul` 的 build-restore-prompt 流水线把子 Agent 的生命周期管理得像函数调用一样干净，再用 `ApprovalSource` 确保每个子任务的权限边界泾渭分明。

---

## 延伸阅读与待办

- [ ] [[Claude-Code-Coordinator-模式源码解析]]
- [ ] [[Claude-Code-Swarm-与-InProcessTeammate-解析]]
- [ ] [[Kimi-CLI-Subagent-生命周期解析]]
- [ ] [[引擎-多Agent-协作设计草案]]
- [ ] [[从零开始的引擎骨架]] —— 已根据本篇洞察修正

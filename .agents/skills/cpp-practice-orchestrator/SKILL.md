---
name: cpp-practice-orchestrator
description: C++ 刻意练习总指挥。当用户在 workspace/cpp-recovery/ 目录下进行 C++ 手写练习时触发，负责读取练习状态、引导进入正确的子流程、定义完整的练习生命周期。触发词包括"开始今天的练习"、"今天练完了"、"结束练习"、"继续练习"。
---

# C++ 刻意练习 Orchestrator

## 定位

本 Skill 是 C++ 70 天恢复练习的**总指挥**，不直接处理代码评审或提示，而是：
1. 读取当前练习状态
2. 判断用户处于练习周期的哪个阶段
3. 引导用户进入对应的子 Skill

## 触发条件

用户在 `workspace/cpp-recovery/` 相关语境下说出以下任何一种：
- "开始今天的练习" / "开始练习" / "今天练什么"
- "今天练完了" / "结束练习" / "完成今天的任务"
- "继续练习" / "接着昨天的"
- "我的进度怎么样" / "练到哪里了"

## 练习生命周期

```
开始练习
  │
  ├─→ 读取 state.json，确认当前 Day 和任务
  ├─→ 展示当天题目（从计划笔记提取）
  │
  ▼
手写代码阶段（用户在编辑器中写代码）
  │
  ├─→ 用户说"卡住了" / "给点提示" → 触发 cpp-practice-hint → **记录交互日志**
  ├─→ 用户说"帮我看看" / "评审一下" → 触发 cpp-practice-review → **记录交互日志**
  │
  ▼
代码提交后
  │
  ├─→ 用户请求评审 → 触发 cpp-practice-review → **记录交互日志**
  ├─→ 评审通过后 → 建议进行 cpp-practice-check
  │
  ▼
结束当天练习
  │
  ├─→ 触发 cpp-practice-check（知识校验）→ **记录交互日志**
  ├─→ 触发 cpp-practice-tracker（记录&评估）← **读取完整交互日志**
  ├─→ 触发 cpp-practice-planner（计划调整建议）
  ├─→ 归档加密会话日志（可选）
  └─→ 更新 state.json 和计划笔记
```

## 工作流程

### 第一步：读取状态

1. 读取 `workspace/cpp-recovery/.practice-tracker/state.json`
2. 读取 `Notes/学习计划/C++基础恢复70天计划.md` 获取当天题目详情
3. 输出当前进度摘要：
   ```
   📅 当前进度：Day X / 70（第 Y 周）
   📝 今日任务：<taskName>
   🎯 本周主题：<weekTheme>
   📊 当前能力概览：syntax=X pointer=Y memoryMgmt=Z ...
   ```

### 第二步：引导进入正确阶段

根据用户意图和当前状态，给出明确的下一步指引：

| 用户状态 | AI 响应 |
|---------|---------|
| 刚开始今天，还没写代码 | "今天的任务是：... 请先手写代码，写完后告诉我'帮我看看'" |
| 写了一半，卡住了 | "需要提示吗？说'给点提示'，我会按层级给你线索，不会直接给答案。" |
| 写完了，请求评审 | 确认代码文件路径，告知即将触发 Review Skill |
| 评审通过，准备结束 | "要进行知识校验吗？说'考考我'来检验今天的掌握度。" |
| 校验完成，结束当天 | 依次调用 Check → Tracker → Planner，输出总结 |

### 第三步：状态更新

任何子 Skill 执行完毕后，Orchestrator 负责将关键信息写回 `state.json`：
- 最后活跃日期
- 当前天数（仅在确认完成当天练习后推进）
- 提交记录
- 能力变化

## 交互日志系统（跨会话持久化）

为了解决"换窗口丢失上下文"问题，每次 AI 与用户的交互都必须写入**本地会话日志**。

### 日志格式

文件路径：`.practice-tracker/sessions/day{NN}_YYYY-MM-DD.jsonl`

每行一个 JSON 对象：
```json
{"ts":"2026-06-03T14:30:22","day":1,"taskId":"day01_string_basic","type":"review","round":1,"payload":{...}}
```

`type` 枚举：`review` / `hint` / `check` / `revision` / `chat`

### 记录时机

| 交互类型 | 记录内容 | 写入方式 |
|---------|---------|---------|
| Review | 代码快照、发现的 issues、评审摘要 | AI 用 Shell echo >> jsonl |
| Hint | 提示级别(L1/L2/L3)、提示主题 | AI 用 Shell echo >> jsonl |
| Check | 问题、用户回答、评分 | 逐题追加 |
| Revision | 用户修改后的代码、修改说明 | 用户说"改好了"时记录 |

### 归档与加密

用户可选择将日志加密后上传 git：
```bash
python scripts/session_tool.py archive-all   # 压缩+加密到 archive/
python scripts/session_tool.py decrypt-all   # 解密还原
```

- `sessions/` 目录被 `.gitignore` 忽略（明文不上传）
- `archive/` 目录可提交（加密后隐私安全）
- 密钥保存在 `.practice-tracker/.key`（被 gitignore）或环境变量 `CPP_RECOVERY_KEY`

## 跨 Skill 协调规则

1. **Review 之后**：如果评审通过，询问是否进行知识校验；如果还有问题，询问是否继续修改或要提示。**无论通过与否，都要写入交互日志。**
2. **Hint 之后**：提示使用后必须告知用户剩余提示次数（L1/L2/L3 各一次，用完即止）。**写入 hint 交互日志。**
3. **Check 之后**：根据掌握度决定是否可以结束当天。如果薄弱点过多，建议再复习或加练。**逐题写入 check 交互日志。**
4. **Tracker 之后**：输出能力雷达图变化，标记弱项。**Tracker 必须读取当天完整的交互日志，而非仅靠 state.json 摘要。**
5. **Planner 之后**：输出计划调整建议，需用户确认后才修改计划笔记

## 注意事项

- **不越俎代庖**：Orchestrator 只做引导和状态管理，具体评审/提示/校验逻辑交给子 Skill
- **状态文件是唯一的真实来源**：所有子 Skill 必须读写同一个 state.json
- **尊重用户节奏**：用户说"我还没准备好结束"时，不要强行推进到 Check/Tracker/Planner

# Vault 配置指南

## 目录结构

```
<vault-root>/
├── Game/          # 公司游戏项目笔记（保密，已.gitignore）
├── GameLearn/     # 从 Game/ 提炼的通用工程知识（可公开）
├── workspace/     # 代码工作区
├── AI/            # AI 相关笔记
├── C++/           # 通用技术知识
└── .claude/       # Claude 配置（skills、hooks）
```

**GameLearn 提炼规则**：
1. 严禁包含公司源代码敏感信息（代码片段、API、密钥等）
2. 仅提炼通用工程知识、设计思想、最佳实践
3. 建议从 Game/ 提炼总结，而非直接创建

## Skill 与 Hooks

- **存放位置**：`<vault-root>/.claude/`
- **优先级**：Vault 级 > 用户级 (`~/.claude/`)
- **常用**：`/sync-gamelearn` — Game/ 变更时更新 GameLearn/

## 跨平台兼容

| 项目 | 规范 |
|------|------|
| 编译器 | GCC (g++) |
| 构建 | `cmake --build build` |
| 路径 | 正斜杠 `/`，用 `<vault-root>` 占位 |
| 换行 | LF（.gitattributes 控制）|

## 文档规范

- 图表宽度 800-1000px
- 命令示例跨平台兼容

## 交互偏好

- **忽略未提及的 Obsidian 上下文**：用户未提及笔记、选中文本、文件名时，忽略 hook 注入的上下文

## 规则管理

- **默认位置**：新规则默认添加在 CLAUDE.md，除非用户明确指定其他位置
- **自动压缩**：添加规则后自动尝试压缩精简

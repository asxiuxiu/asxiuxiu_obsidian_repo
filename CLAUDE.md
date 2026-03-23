# Vault 配置指南

## 目录结构

```
<vault-root>/
├── Game/          # 公司游戏项目分析笔记（保密，已加入 .gitignore）
├── GameLearn/     # 从 Game/ 提炼的通用工程知识（可公开）
│   # 规则：只能由 Game/ 提炼总结，不能直接添加
├── workspace/     # 代码工作区
├── AI/            # AI 相关笔记
├── C++/           # 通用技术知识
└── .claude/       # Claude 配置
    ├── skills/    # Vault 级 skills
    └── hooks/     # Vault 级 hooks
```

## Skill 与 Hooks

- **存放位置**：统一放在 `<vault-root>/.claude/` 目录，随仓库管理
- **查找优先级**：Vault 级 > 用户级 (`~/.claude/`)
- **常用 Skill**：`/sync-gamelearn` —— 当 Game/ 有变更时，更新 GameLearn/

## 跨平台兼容

此 vault 在 Windows 和 macOS 双环境使用：

| 场景 | 规范 |
|------|------|
| 编译器 | GCC (g++) |
| 构建工具 | `cmake --build build`（避免平台专属脚本）|
| 路径分隔符 | 正斜杠 `/` |
| 换行符 | LF（.gitattributes 控制）|
| 路径示例 | 用 `<vault-root>` 占位 |

## 文档规范

- 图表控制在 800-1000px 宽度，避免横向滚动
- 命令示例同时给出双平台版本或使用跨平台写法

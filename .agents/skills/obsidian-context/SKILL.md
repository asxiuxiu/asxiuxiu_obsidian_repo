---
name: obsidian-context
description: 读取 Obsidian 当前激活笔记和选中文本，注入到对话上下文中。当用户需要基于当前 Obsidian 笔记进行讨论时使用。
---

# Obsidian 上下文注入

当用户需要基于当前 Obsidian 笔记进行讨论时，使用此 skill 读取激活笔记内容。

## 使用场景

- 用户说"帮我分析这篇笔记"、"总结当前文件"、"优化这段内容"
- 用户提到了笔记文件名但你没有上下文
- 用户想要基于 Obsidian 中选中的文本进行操作

## 使用方法

使用 Obsidian CLI 命令获取上下文信息。Obsidian 必须处于运行状态。

### macOS / Linux (bash / zsh)

```bash
# 获取当前激活文件信息
obsidian file active

# 获取当前激活文件内容
obsidian read active

# 获取当前选中的文本
obsidian eval code="app.workspace.activeEditor?.editor?.getSelection() ?? ''"
```

### Windows (PowerShell)

```powershell
# 获取当前激活文件信息
obsidian file active

# 获取当前激活文件内容
obsidian read active

# 获取当前选中的文本
obsidian eval code="app.workspace.activeEditor?.editor?.getSelection() ?? ''"
```

### 各平台注意事项

| 平台 | 注意点 |
|------|--------|
| **macOS / Linux** | `obsidian` 命令通常位于 `/Applications/Obsidian.app/Contents/MacOS/obsidian`（macOS）或安装目录。确保已加入 `PATH`，否则使用绝对路径。 |
| **Windows** | 使用 `;` 作为命令分隔符，而非 `&&`。切换目录用 `cd` 或 `Set-Location`。错误处理用 `try/catch`。 |

```powershell
# Windows PowerShell 示例
cd D:\obsidian_lib; obsidian file active
```

## 构建上下文

将获取到的信息整理成以下格式注入对话：

```
## Obsidian 激活笔记上下文
**笔记**: {文件名}
**路径**: {文件路径}

### 笔记内容
```markdown
{文件内容}
```

### 当前选中文本（如有）
```
{选中的文本}
```
```

## 注意事项

1. 获取不到激活文件时（返回空或报错），告知用户未检测到激活笔记
2. 选中文本为空时，不显示"当前选中文本"部分
3. 此 skill 作为前置步骤执行，获取上下文后再处理用户请求
4. 在 Windows 环境下确保使用 PowerShell 语法，避免使用 Bash 特有的 `&&`、`||` 操作符

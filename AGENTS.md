# Vault 配置指南

## 目录结构

```
<vault-root>/
├── Notes/         # 知识笔记（无数字前缀）
│   ├── 计算机图形学/
│   ├── 游戏引擎/
│   ├── C++编程/
│   ├── 数学基础/
│   ├── 人工智能/
│   ├── 构建系统/
│   ├── 学习计划/
│   └── 索引/      # MOC、总索引
├── Assets/        # 图片资源
│   ├── games/graphics/math/ai/
├── workspace/     # 代码实践
├── Game/          # 公司源码分析（不可上传 git）
└── .agents/       # Agent Skills
```

| 文件夹 | 权限 | 关键约束 |
|--------|------|----------|
| `Notes/` | ✅ 可读写 | **禁止数字序号前缀** |
| `Assets/` | ✅ 可读写 | 图片统一存此，小写+连字符命名 |
| `workspace/` | ✅ 可读写 | — |
| `Game/` | ✅ 可读写 | 源码分析笔记专属，**禁传 git** |

## 跨平台与 Shell

- 编译器 GCC，构建 `cmake --build build`，路径用 `/`，换行 LF
- 环境：Windows PowerShell 5.1，**禁用 `&&` / `||`**
- 文件修改优先用 `ReadFile/WriteFile/StrReplaceFile/Glob`
- 中文路径/读写文件时显式加 `-Encoding UTF8`；git 中文报错用 `git restore --source=HEAD -- "path"`

## Obsidian 技能

处理以下任务时**优先读取对应 skill**：

| Skill | 触发场景 |
|-------|----------|
| `obsidian-context` | 用户提到“当前笔记/选中内容”时先注入上下文 |
| `obsidian-cli` | 与运行中的 Obsidian 交互（读/写/搜索笔记、开发插件） |
| `obsidian-markdown` | 编写 `.md` 时确保 wikilink、callout、embed、frontmatter 正确 |
| `obsidian-bases` | 创建/编辑 `.base` 数据库视图 |
| `json-canvas` | 创建/编辑 `.canvas` 白板 |

## 笔记规范

### 核心原则
- **无数字前缀**：文件夹、文件名不带 `01-`、`1. ` 等前缀，顺序由索引维护。
- **索引统一命名**：各分类索引文件名为 `索引.md`。
- **导航链接**：每篇笔记顶部（frontmatter 后）添加：
  ```markdown
  > [← 返回 xxx索引]([[索引\|xxx索引]])
  ```

### 源码分析笔记
- 涉及 `chaos`/`wolfgang`/`proven_ground` 的笔记**必须放入 `Game/`**。
- 命名格式：`<模块>-源码解析：<主题>.md`

### 维护 checklist
1. 去掉新增文件/文件夹的数字前缀
2. 维护对应 `索引.md` 的学习顺序与返回链接
3. 同步更新 `索引/知识总索引.md`
4. 确认源码分析笔记归档在 `Game/`

## 文档规范

- 图表宽 800-1000px，代码示例优先 **C++**，命令跨平台兼容
- 多行数学公式分开书写，避免 `\\` 换行
- **表格内 wikilink 转义**：`[[路径\|别名]]`
- 概念解释遵循费曼学习法 **Why → What → How**

### 费曼模板
```markdown
## Why：为什么要学习 X？
- 问题背景 / 不用后果 / 应用场景

## What：X 是什么？
- 核心定义 / 关键概念 / 原理图解

## How：如何使用 X？
- 基本用法 / 最佳实践 / 常见陷阱
```

## 图片与资源

- 引用格式：`![[Assets/分类/图片名]]`
- **强制入库**：所有图片必须下载到 `Assets/`，**禁止外部链接**
- **禁止行为**：不要用 `attachments/` 或 `images/` 子文件夹；不要用时间戳/Pasted image 作为永久文件名

## Git Commit

- 默认格式：`Vault backup: YYYY-MM-DD HH:MM:SS`
- 禁止总结修改内容；如用户主动传参，仍用传入内容
- skill `sync-push` 已内置此规范

## 其他

- 新规则默认添加在 AGENTS.md，添加后自动尝试压缩精简

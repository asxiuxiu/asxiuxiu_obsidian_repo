# Vault 配置指南

## 目录结构

```
<vault-root>/
├── Notes/         # 知识笔记（按主题分类，无数字前缀）
│   ├── 计算机图形学/
│   ├── 游戏引擎/
│   ├── C++编程/
│   ├── 数学基础/
│   ├── 人工智能/
│   ├── 构建系统/
│   ├── 学习计划/
│   └── 索引/      # MOC、总索引
├── Assets/        # 静态资源
├── workspace/     # 代码实践
├── Game/          # 保密，仅读
└── .agents/       # Agent Skills
```

### 文件夹权限

| 文件夹          | 权限    | 说明                |
| ------------ | ----- | ----------------- |
| `Notes/`     | ✅ 可读写 | 按主题分类组织，禁止数字序号前缀  |
| `Assets/`    | ✅ 可读写 | 图片资源              |
| `workspace/` | ✅ 可读写 | 代码实践              |
| Game/        | ✅ 可读写 | 公司源码项目分析，不可上传至git |

## 跨平台兼容

| 项目 | 规范 |
|------|------|
| 编译器 | GCC (g++) |
| 构建 | `cmake --build build` |
| 路径 | 正斜杠 `/`，用 `<vault-root>` 占位 |
| 换行 | LF |

## Shell 规范

环境为 **Windows PowerShell 5.1**。禁止在 PS 5.1 中使用 `&&` / `||`。

| 场景 | 推荐方式 |
|------|---------|
| 简单命令链 | `cmd` |
| 复杂文件操作 | PS 5.1 + `if ($?) { ... }` |
| 中文路径/输出 | 必要时 `$OutputEncoding = [System.Text.Encoding]::UTF8` |
| 文件内容修改 | 优先用 `ReadFile/WriteFile/StrReplaceFile/Glob`，避免 PS 字符串操作 |
| 读写文件 | 必须显式加 `-Encoding UTF8` |
| 中文路径移动失败 | 改用 `cmd /c` 或 Python |
| git 中文报错 | `git restore --source=HEAD -- "path"` |

## 笔记命名与索引规范

**核心原则**：
- **禁止数字序号前缀**：文件夹、文件名均不带 `01-`、`1. ` 等前缀。顺序由索引维护。
- **索引统一命名**：各分类及子系列索引统一为 `索引.md`。
- **导航链接**：每篇笔记顶部（frontmatter 后）添加返回索引链接：
  ```markdown
  > [← 返回 xxx索引]([[索引|xxx索引]])
  ```

**源码分析笔记归档规则**：
- 所有涉及公司游戏项目（`chaos` 引擎、`wolfgang`、`proven_ground`）的源码分析笔记，**必须放入 `Game/` 目录**，按阶段/模块分类归档。
- 通用技术知识（如计算机图形学、C++ 编程、数学概念）可放入 `Notes/` 目录。
- 源码解析笔记默认命名格式：`<模块>-源码解析：<主题>.md`

**维护 checklist**：
1. 检查并去掉新增文件/文件夹的数字序号前缀
2. 在对应分类目录下维护 `索引.md` 的学习顺序
3. 为新增笔记添加返回索引导航链接
4. 同步更新 `索引/知识总索引.md` 及相关交叉引用
5. **源码分析笔记**：确认归档在 `Game/` 目录，而非 `Notes/` 目录

## 文档规范

- 图表宽度 800-1000px
- 命令示例跨平台兼容
- 代码示例优先 **C++**
- 多行数学公式分开书写，避免 `\\` 换行
- **概念解释**：遵循费曼学习法 **Why → What → How**
- **Wikilink 与表格冲突**：Markdown 表格内使用 `[[路径|别名]]` 时，中间的 `|` 会被表格解析为列分隔符导致链接断裂。**表格内必须使用转义写法 `[[路径\|别名]]`**

### 费曼模板

```markdown
## Why：为什么要学习 X？
- **问题背景**：描述 X 解决的核心问题
- **不用 X 的后果**：不使用时会遇到什么困难
- **应用场景**：列出 2-3 个实际应用场景

## What：X 是什么？
- **核心定义**：一句话定义
- **关键概念**：用表格或列表列出核心概念
- **原理图解**：如有必要，使用图表说明

## How：如何使用 X？
- **基本用法**：最简单的使用示例
- **最佳实践**：推荐的使用方式
- **常见陷阱**：容易犯的错误及避免方法
```
## 交互偏好

- **忽略未提及的 Obsidian 上下文**

## 规则管理

- 新规则默认添加在 AGENTS.md
- 添加后自动尝试压缩精简

## 图片资源规范

所有图片统一存放在 `<vault-root>/Assets/`，按主题分类：

```
Assets/
├── games/     # 游戏引擎、渲染
├── graphics/  # 计算机图形学
├── math/      # 数学概念
└── ai/        # AI/ML（预留）
```

- 引用：`![[Assets/分类/图片名]]`
- 命名：小写字母、连字符分隔（如 `rendering-pipeline.png`）
- **强制要求**：所有图片必须下载到仓库内，**禁止外部图片链接**
- **禁止行为**：不要将图片放在笔记目录下的 attachments/ 或 images/ 文件夹；不要使用时间戳或 "Pasted image" 作为永久文件名

## PDF 文本提取

**工具位置**: `<vault-root>/.agents/skills/pdf-text-extractor/pdf_text_toolkit.py`

```bash
# 提取全部文本
python pdf_text_toolkit.py text "document.pdf" --output=content.txt

# 提取代码片段
python pdf_text_toolkit.py code "document.pdf" --format=json

# OCR 识别
python pdf_text_toolkit.py ocr "scanned.pdf" --lang=chi_sim+eng

# 搜索文本
python pdf_text_toolkit.py search "document.pdf" "keyword" --context=100
```

**依赖**: `pip install PyMuPDF`

**OCR 额外依赖**: `pip install pytesseract pillow`（需安装 Tesseract）

## Git Commit 规范

| 项目 | 规范 |
|------|------|
| 默认格式 | `Vault backup: YYYY-MM-DD HH:MM:SS` |
| 内容描述 | 禁止总结修改内容 |
| 自定义信息 | 如用户主动传入参数，仍使用传入内容 |

> skill `sync-push` 已内置此规范。

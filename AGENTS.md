# Vault 配置指南

## 目录结构

```
<vault-root>/
├── Notes/              # 📝 知识笔记分类存储
│   ├── 01-计算机图形学 # 渲染、图形学理论
│   ├── 02-游戏引擎     # 引擎架构、ECS、网络同步等
│   ├── 03-C++编程      # C++语言、STL、现代C++
│   ├── 04-数学基础     # 线性代数、几何、计算数学
│   ├── 05-人工智能     # AI/ML相关
│   ├── 06-构建系统     # CMake、编译、工具链
│   ├── 07-学习计划     # 路线图、方法论
│   └── 99-索引         # MOC、索引页
├── Assets/             # 🖼️ 静态资源（图片等）
├── workspace/          # 💻 代码实践
├── Game/               # 公司游戏项目笔记（保密，已.gitignore）
└── .agents/            # Agent Skills
```

### 文件夹权限规则

| 文件夹          | 权限       | 说明                |
| ------------ | -------- | ----------------- |
| `Notes/`     | ✅ AI 可读写 | 分类知识笔记，按数字编号组织    |
| `Assets/`    | ✅ AI 可读写 | 图片资源              |
| `workspace/` | ✅ AI 可读写 | 代码实践              |
| `Game/`      | ⚠️ 仅读取   | 保密内容，AI 只能读取，不得修改 |


## 跨平台兼容

| 项目 | 规范 |
|------|------|
| 编译器 | GCC (g++) |
| 构建 | `cmake --build build` |
| 路径 | 正斜杠 `/`，用 `<vault-root>` 占位 |
| 换行 | LF（.gitattributes 控制）|

## Shell 使用规范

用户环境为 **Windows PowerShell 5.1**（非 PowerShell 7），为避免兼容性问题：

| 场景 | 推荐 Shell | 注意事项 |
|------|-----------|---------|
| 简单命令链 (`&&`) | **cmd** | PowerShell 5.1 不支持 `&&`，用 cmd 更可靠 |
| 复杂文件操作、条件判断 | PowerShell 5.1 | 避免使用 `&&`，改用 `if ($?) { ... }` |
| 涉及中文路径/输出 | 视情况选择 | 必要时显式设置编码 `$OutputEncoding = [System.Text.Encoding]::UTF8` |

**禁止行为**：
- 在 PowerShell 5.1 中使用 `&&` 或 `||` 操作符
- 假设默认编码为 UTF-8（中文 Windows 默认为 GBK）

## 文档规范

- 图表宽度 800-1000px
- 命令示例跨平台兼容
- **概念解释**：遵循费曼学习法，按 Why? → What? → How? 三层次展开
- **代码示例**：尽量使用 **C++**，与游戏开发技术栈保持一致

### 费曼学习法笔记规范

#### 核心原则

> "如果你不能简单地解释它，你就还没有真正理解它。" —— 理查德·费曼

每篇概念性笔记应包含三个层次：

| 层次 | 问题 | 内容要求 | 检验标准 |
|------|------|---------|---------|
| **Why** | 为什么要学这个？ | 解决什么问题？不用会怎样？ | 能用一句话说清价值 |
| **What** | 这是什么？ | 核心概念、原理、公式 | 能向一个初学者解释清楚 |
| **How** | 如何用？ | 代码实现、最佳实践、常见陷阱 | 能在实际项目中应用 |

#### 模板示例

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

#### 优秀示例参考

- [[Notes/07-学习计划/从零到入土的图形学学习.md]] — Why/What/How Good 结构
- [[Notes/03-C++编程/C++ 值类别与移动语义]] — 完整的 Why → What → How 流程
- [[Notes/04-数学基础/线性代数的本质/00-索引]] — 系列知识的层次组织

## 交互偏好

- **忽略未提及的 Obsidian 上下文**：用户未提及笔记、选中文本、文件名时，忽略自动注入的上下文

## 规则管理

- **默认位置**：新规则默认添加在 AGENTS.md，除非用户明确指定其他位置
- **自动压缩**：添加规则后自动尝试压缩精简

## 图片资源管理规范

所有图片资源统一存放在 `<vault-root>/Assets/` 目录下，按主题分类：

```
Assets/
├── games/        # 游戏引擎、渲染相关图片
├── graphics/     # 计算机图形学相关图片
├── math/         # 数学概念、公式图示
└── ai/           # AI/机器学习相关图片（预留）
```

**使用规范**:
- **引用方式**: 使用 Obsidian Wikilink 语法 `![[Assets/分类/图片名]]`
- **命名规范**: 使用小写字母、连字符分隔，语义清晰（如 `rendering-pipeline.png`）
- **强制要求**: **所有图片必须下载到仓库内，禁止任何外部图片链接**（防止链接失效/404）
- **禁止行为**: 
  - 不要将图片放在笔记目录下的 attachments/ 或 images/ 文件夹
  - 不要使用时间戳或 "Pasted image" 作为永久文件名
  - **禁止在笔记中使用 `https://` 或 `http://` 开头的图片 URL**

## PDF 文本提取规范

当需要从 PDF 提取文字内容、代码片段时，使用以下工具：

**工具位置**: `<vault-root>/.agents/skills/pdf-text-extractor/pdf_text_toolkit.py`

**使用步骤**:
1. **提取文本**: 使用 `extract_text()` 或 `extract_all_text()` 获取纯文本
2. **提取代码**: 使用 `extract_code_blocks()` 自动识别代码片段
3. **OCR 识别**: 对扫描版 PDF 使用 `ocr_page()` 或 `ocr_all_pages()`
4. **文本搜索**: 使用 `search_text()` 定位特定内容

**命令行示例**:
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

**OCR 额外依赖**: `pip install pytesseract pillow`（需安装 Tesseract 引擎）

## Git Commit 规范

**原则**：此仓库是 Obsidian 笔记备份仓库，commit 信息仅作为时间戳标记，具体内容由文件本身承载。

| 项目 | 规范 |
|------|------|
| 默认格式 | `Vault backup: YYYY-MM-DD HH:MM:SS` |
| 内容描述 | 禁止总结修改内容（如"新增xxx笔记"、"更新xxx"） |
| 自定义信息 | 如用户主动传入参数，仍使用传入内容 |

**注意**：skill `sync-push` 已内置此规范。

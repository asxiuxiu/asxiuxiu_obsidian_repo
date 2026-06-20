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
- 环境：Git Bash（MSYS2），支持 `&&` / `||` 命令链
- 文件修改优先用 `ReadFile/WriteFile/StrReplaceFile/Glob`
- 中文路径/读写文件时确保 UTF-8 编码；git 中文报错用 `git restore --source=HEAD -- "path"`

## Obsidian 技能

处理以下任务时**必须读取对应 skill**，禁止跳过：

| Skill | 触发场景 |
|-------|----------|
| `obsidian-context` | 用户提到"当前笔记/选中内容"时先注入上下文 |
| `obsidian-cli` | 与运行中的 Obsidian 交互（读/写/搜索笔记、开发插件） |
| `obsidian-markdown` | 编写 `.md` 时确保 wikilink、callout、embed、frontmatter 正确 |
| `obsidian-bases` | 创建/编辑 `.base` 数据库视图 |
| `json-canvas` | 创建/编辑 `.canvas` 白板 |
| `note-refine` | 优化笔记叙述、检查笔记质量、用费曼学习法重构笔记 |
| `cpp-practice-orchestrator` | 用户在 `workspace/cpp-recovery/` 进行 C++ 刻意练习时，协调各子 Skill 工作流 |
| `cpp-practice-review` | 用户手写代码后请求评审，四维度代码评审（接口/正确性/优化/标准库对比） |
| `cpp-practice-hint` | 用户卡住时请求提示，三级渐进提示（L1方向→L2线索→L3填空），严防给答案 |
| `cpp-practice-tracker` | 记录练习数据，维护6维能力矩阵，输出能力趋势和弱项预警 |
| `cpp-practice-planner` | 根据能力评估动态调整后续计划，支持合并/拆分/插入巩固题 |
| `cpp-practice-check` | 每天结束后知识校验，3-5个追问检验当天知识点掌握度 |

## 笔记规范

笔记规范分为三个层面：**格式规范**（Obsidian 语法、链接、frontmatter）、**内容规范**（知识准确性、网络性）、**叙述原则**（费曼精神）。

### 一、格式规范

格式规范定义 Obsidian 笔记的语法正确性，写笔记时参考 `obsidian-markdown` skill。

- **无数字前缀**：文件夹、文件名不带 `01-`、`1. ` 等前缀，顺序由索引维护。
- **文件名跨平台兼容**：文件名禁止包含 Windows 非法字符（`< > : " / \ | ? *`），避免在 Windows 上无法检出或同步失败。
- **索引统一命名**：各分类索引文件名为 `索引.md`。
- **导航链接**：每篇笔记顶部（frontmatter 后）添加：
  ```markdown
  > [[索引|← 返回 xxx索引]]
  ```
- **完整路径 wikilink**：跨目录链接必须从 vault root 写完整路径，如 `[[Notes/C++编程/xxx]]`，禁止省略 `Notes/` 前缀。
- **精确锚点链接**：如果目标笔记中有专门讲解该概念的小节，链接必须指向具体标题（`[[路径#标题|别名]]`），禁止只链接到整篇笔记。
- **表格内转义**：wikilink 在表格中必须转义管道符，格式为 `[[路径\|别名]]`。
- **修改前置**：任何对 `.md` 笔记的修改操作**必须先读取 `obsidian-markdown` skill**。

### 二、内容规范

内容规范定义知识网络的基础规则。

- **零假定 + 精确链接（首次出现双义务）**：笔记中首次出现任何概念名词或缩写时，必须同时完成两件事：
  1. **零假定**：不得假定读者已知——先用 1~2 句人话解释"这是什么东西"，再给正式名称/缩写。
  2. **精确链接**：如果 vault 中已有其他笔记讲过这个概念，必须添加 wikilink **指向该笔记中讲解此概念的具体标题**（`[[路径#标题|别名]]`）。不要只链接到整篇笔记，也不要只在文中说"前面讲过"却不给链接。
- **示例**：
  ```markdown
  ❌ 操作系统内核中为每个线程维护一个 TCB（Thread Control Block）。
  ✅ 线程被中断时，操作系统需要把它的执行现场保存下来——程序计数器、寄存器值、栈位置等。这些状态信息被打包存在一个结构体里，叫做 **TCB（Thread Control Block，线程控制块）**。
  ```

### 三、叙述原则（费曼精神）

> **写笔记的本质是思考，不是记录。**
>
> 费曼说："这不是记录，这就是我思考本身。" 你的笔记应该呈现**你真正理解后的语言与逻辑**，而不是对原文的转述或摘抄。

**没有任何强制分段、没有固定标题结构、没有必须遵守的格式模板。** 唯一的要求是：

> **当你写完这篇笔记，一个对此概念零基础的人能否顺畅地读完并理解？**
>
> 检验方法：把笔记里的比喻和例子全部删掉，剩下的文字是否还能自洽？如果不能，说明术语没有建立直觉就抛出来了。

**详细的写作策略、自检清单、优化手法，参考 `note-refine` skill。** 该 skill 提供了一套完整的诊断和优化流程，包括：

- 10 条可选写作策略（Why 先行、比喻贯穿、因果推导、场景绑定、问题链衔接、用自己的话写、原子化、渐进式摘要、有效失败、表格只用于总结）
- 7 维度自检清单（零假定、Why 先行、比喻有效、因果链条、衔接自然、表格合规、链接完整）
- 从"分类罗列"到"因果推导"的具体改写技巧
- 术语人话解释的模板

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

## Run Code 插件（C++ 代码片段）

本 vault 的 `.obsidian/plugins/obsidian-run-code` 插件用于在 Obsidian 内直接运行 C++ 代码块。代码块语言标签**统一使用 `cpp`**，编译参数通过**顶部注释**控制。

### 编译参数注释格式

在代码块前 20 行内放置 `// flags:` 注释：

```cpp
// flags: -O0 -g -fsanitize=address
#include <iostream>
int main() {
    // 这段代码会以 -std=c++20 -Wall -O2 -O0 -g -fsanitize=address 编译
    return 0;
}
```

| 注释写法 | 效果 |
|---------|------|
| `// flags: -O0 -g` | **追加参数**到默认参数之后（默认：`-std=c++20 -Wall -O2`） |
| `// flags: override -O3 -DNDEBUG` | **完全替换**默认参数，只用 `-O3 -DNDEBUG` |

### 常用配置示例

| 场景 | 注释 |
|------|------|
| 调试模式（无优化 + 调试符号） | `// flags: -O0 -g` |
| Release 模式 | `// flags: override -O3 -DNDEBUG` |
| AddressSanitizer | `// flags: -fsanitize=address -fno-omit-frame-pointer` |
| 严格警告 | `// flags: -Wextra -Werror -pedantic` |
| 链接外部库 | `// flags: -lSDL2` |

### 规范要点

1. **不加空格**：`flags:` 与参数之间用一个空格分隔，参数之间也用空格分隔。
2. **放在代码块顶部**：`// flags:` 注释尽量放在 `#include` 之前第一行，便于一眼识别。
3. **不混用多行**：所有编译参数写在一行 `// flags:` 注释中，不支持多行累积。
4. **代码块语言保持 `cpp`**：不要使用 `cpp-debug` 等变体，避免代码高亮失效。

## 图片与资源

- 引用格式：`![[Assets/分类/图片名]]`
- **强制入库**：所有图片必须下载到 `Assets/`，**禁止外部链接**
- **禁止行为**：不要用 `attachments/` 或 `images/` 子文件夹；不要用时间戳/Pasted image 作为永久文件名

## Git Commit

- 默认格式：`Vault backup: YYYY-MM-DD HH:MM:SS`
- 禁止总结修改内容；如用户主动传参，仍用传入内容
- skill `sync-push` 已内置此规范

## C++ 刻意练习工作流

当用户在 `workspace/cpp-recovery/` 目录下进行 C++ 手写练习时，按以下优先级执行：

1. **先读取 `cpp-practice-orchestrator`**，确认当前练习阶段和进度
2. **手写代码时卡住** → 触发 `cpp-practice-hint`（三级渐进提示，严防给答案）
3. **写完后请求评审** → 触发 `cpp-practice-review`（四维度评审，严格对齐当天目标不超纲）
4. **结束一天练习时** → 依次触发：
   - `cpp-practice-check`（知识校验，3-5个追问）
   - `cpp-practice-tracker`（记录数据，更新6维能力矩阵）
   - `cpp-practice-planner`（根据评估动态调整后续计划）
5. **所有交互后**更新 `workspace/cpp-recovery/.practice-tracker/state.json`

**练习状态文件**：`workspace/cpp-recovery/.practice-tracker/state.json` 是唯一的真实数据源，所有 Skill 必须读写此文件以保持状态一致。

## Quartz 网站迭代工作流

本 vault 通过 GitHub Pages 发布为 Quartz v5 静态网站。仓库包含两个关键分支：

| 分支 | 作用 | 主要文件 |
|------|------|----------|
| `main` | Obsidian vault 源码 | `Notes/`、`Assets/`、`.github/workflows/deploy-notes.yml` |
| `v5` | Quartz 网站工程 | `quartz.config.yaml`、`quartz.ts`、`quartz/styles/custom.scss` |

### 修改范围

- **站点标题 / 字体 / 配色 / 组件配置** → 改 `v5` 分支的 `quartz.config.yaml`
- **布局逻辑 / Explorer 排序 / 插件选项覆盖** → 改 `v5` 分支的 `quartz.ts`
- **样式细节 / 移动端适配 / 设计感** → 改 `v5` 分支的 `quartz/styles/custom.scss`
- **首页文案 / 部署触发条件** → 改 `main` 分支的 `.github/workflows/deploy-notes.yml`

### 分支切换与并发修改

两个分支都需要改时，优先用 `git worktree` 避免来回切换导致未提交修改冲突：

```bash
git worktree add ../asxiuxiu_obsidian_repo-v5 v5
# main 继续留在原仓库根目录操作
```

### 快速迭代技巧

全量构建（200+ 篇笔记）很慢，调试样式/脚本时应先精简 `content/`：

```bash
# 保留 1-2 个测试页面即可
rm -rf content
mkdir -p content/Notes/xxx
# 复制一篇测试笔记进去
npx quartz build
```

验证没问题后再恢复完整 `content/` 做一次全量构建。

### 本地验证

```bash
# 1. 起本地服务
python3 -m http.server 8080 --directory public

# 2. 用 puppeteer + Chrome 截图看移动端效果
npx puppeteer-core --...  # 或写脚本用 Chrome headless 截图
```

### 常见坑

1. **`quartz.ts` 中传给插件的回调会序列化到浏览器端执行**。例如 `ExternalPlugin.Explorer({ sortFn: ... })` 的 `sortFn` 会被 `JSON.stringify` 后交给客户端 `eval`，因此：
   - 不能引用外部作用域的函数/变量
   - 不能使用命名函数或箭头函数赋值给变量，否则 esbuild 的 `keepNames` 会注入 `__name` 辅助函数，导致客户端 `ReferenceError: __name is not defined`
   - 解决方案：把逻辑完全内联，只使用原始循环/条件语句

2. **移动端 `.sidebar.left` 默认是 `flex-direction: row`**。如果想让目录、标题、搜索在移动端垂直排列，必须显式覆盖为 `column`。

3. **移动端 Explorer 默认被折叠成 34px 宽的汉堡按钮**。要让目录索引默认可见，需要覆盖 `.explorer.collapsed` 的 `flex` 和 `.explorer-content` 的 `transform/visibility/width`。

### 提交顺序

1. 先提交 `v5` 分支的 Quartz 改动
2. 再提交 `main` 分支的 workflow 改动
3. push 后 GitHub Actions 会自动用最新 v5 配置部署网站

## 其他

- 新规则默认添加在 AGENTS.md，添加后自动尝试压缩精简

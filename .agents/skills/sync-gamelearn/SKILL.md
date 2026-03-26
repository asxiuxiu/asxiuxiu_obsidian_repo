---
name: sync-gamelearn
description: 检测 Game/ 文件夹的变更，针对新增或修改的笔记，在 GameLearn/ 中重新提炼深度分析笔记。
disable-model-invocation: false
---

# Sync GameLearn

当 `Game/` 目录下的笔记发生新增或修改时，在 `GameLearn/` 目录下同步更新对应的深度分析笔记。

## 执行逻辑

### 第一步：检测变更

运行以下命令，找出 `Game/` 目录下有哪些文件变更（相对于上一次提交）：

```bash
git diff HEAD --name-only -- Game/
git status --short Game/
```

同时列出 `Game/` 下所有 `.md` 文件，与 `GameLearn/` 下的文件做比对，找出：
- **新增的** Game/ 笔记（GameLearn/ 中还没有对应文件）
- **修改的** Game/ 笔记（GameLearn/ 中已有文件，但 Game/ 源文件更新了）

### 第二步：逐文件处理

对每一个需要更新的文件：

1. 读取 `Game/<filename>.md` 的完整内容
2. 如果 `GameLearn/<filename>.md` 已存在，也读取它，了解当前分析的结构和侧重点
3. 判断 Game/ 文件中哪些部分是新增的或修改的
4. 针对**变更的部分**，在 GameLearn/ 对应笔记中进行以下分析：
   - **为什么** 这样设计？背后的工程原因是什么？
   - **利弊分析**：这种做法有什么好处和坏处？
   - **通用化**：这个设计决策在其他项目/场景中有哪些相似的实践？
   - **补充知识**：必要时搜索相关资料，补充行业背景

5. 更新 `GameLearn/<filename>.md`，保持已有的良好分析，只更新与变更相关的章节

### 第三步：输出汇总

完成后告知用户：
- 处理了哪些文件
- 每个文件更新了哪些部分

## 规范

- GameLearn/ 的笔记不复制 Game/ 的原始内容，只做分析和提炼
- 不涉及公司保密信息（项目名称、具体路径等可适度抽象化）
- 分析要有深度：不只是描述"做了什么"，更要解释"为什么"和"有什么影响"
- 文件名与 Game/ 中保持一致
- frontmatter 中添加 `source: Game/<filename>.md` 记录来源

## 参数

- `$ARGUMENTS`：可选，指定要同步的文件名（不带路径，如 `引擎脚本构建分析`）。
  若不指定，则检测所有变更文件并批量处理。

# C++ 基础恢复 70 天练习工作区

> 📝 源自笔记：[[workspace/cpp-recovery/C++基础恢复70天计划]]
> 📅 生成时间：2026-06-02

## 简介

本工作区用于 10 周渐进式 C++ 恢复练习，每天一道题，从「写对一个类」逐步爬升到「手写引擎模块」。

## 目录结构

```
cpp-recovery/
├── CMakeLists.txt          # 根构建文件
├── PROGRESS.md             # 进度追踪表（每天在此打勾）
├── include/common.h        # 轻量测试宏（CHECK_EQ / CHECK_TRUE / RUN_TEST）
├── week01/
│   ├── CMakeLists.txt
│   └── day01_string_basic/
│       ├── CMakeLists.txt
│       └── main.cpp        # ← 在此写每日练习
├── .vscode/
│   ├── tasks.json          # Ctrl+Shift+B 构建
│   └── launch.json         # F5 调试
├── .practice-tracker/
│   └── state.json          # AI 能力评估数据
└── README.md
```

## 快速开始

**首次构建（终端）：**
```bash
cd workspace/cpp-recovery
export PATH="/c/msys64/ucrt64/bin:$PATH"
cmake -S . -B build -G Ninja
cmake --build build
```

**运行当天练习：**
```bash
./build/week01/day01_string_basic/day01_string_basic.exe
```

**VS Code 快捷键：**
- `Ctrl+Shift+B` — 构建全部
- `F5` — 调试当前配置的目标（默认 Day 1）

> ⚠️ **PATH 注意**：如果 `/mingw64/bin` 在 PATH 中排在 `/c/msys64/ucrt64/bin` 前面，会导致编译器加载错误 DLL 而静默失败。VS Code 的 tasks.json / launch.json 已自动将 ucrt64 前置；终端中手动构建时请确保 `export PATH="/c/msys64/ucrt64/bin:$PATH"`。

## 每日流程

### 手写阶段

1. 查看当天任务：打开 `PROGRESS.md`（或 vault 中的 `workspace/cpp-recovery/C++基础恢复70天计划.md`）
2. 在工作区新建/打开当天的 `main.cpp`
3. 实现代码，填写 TODO

### AI 辅助阶段

本工作区配套了一套 AI Skill 框架，Skill 文件位于本工作区目录的 `.agents/skills/cpp-practice-*/` 下。你从本工作区启动 Kimi CLI 时，这些 Skill 会自动被识别，无需额外配置。

在 Kimi CLI 中通过自然语言即可调用：

| 你的说法 | 触发的 Skill | 得到什么 |
|---------|-------------|---------|
| "帮我看看这段代码" / "评审一下" | `cpp-practice-review` | 四维度评审报告（接口/正确性/优化/标准库对比），**不超纲** |
| "卡住了" / "给点提示" | `cpp-practice-hint` | 渐进提示：L1方向→L2线索→L3填空，**不会直接给答案** |
| "考考我" / "掌握了没有" | `cpp-practice-check` | 3-5道追问，检验当天知识点 |
| "今天练完了" / "结束练习" | `cpp-practice-orchestrator` | 自动走完全流程：校验→记录→计划调整建议 |
| "我的水平怎么样" / "记录今天的练习" | `cpp-practice-tracker` | 6维能力雷达图 + 弱项预警 |
| "调整计划" / "后面怎么安排" | `cpp-practice-planner` | 根据你的真实水平，建议合并/拆分/加速/放缓 |

### 完整的一天示例

```
你：开始今天的练习
AI：今天 Day 3，任务是「给 String 加上移动构造和移动赋值」...

[你在编辑器里写代码...]

你：卡住了，移动赋值不知道怎么写
AI：💡 提示 L1：想想移动语义的核心目标是什么？...

你：再具体点
AI：💡 提示 L2：移动赋值接收一个右值引用参数...

[你写完代码...]

你：帮我看看
AI：📋 评审报告：✅ 做得好的... ⚠️ 建议改进... 📚 与标准库对比...

你：考考我
AI：📝 第 1/4 题：移动构造的参数为什么是右值引用？...

你：今天练完了
AI：📊 校验结果... 📈 能力更新... 📋 明天计划建议...
```

### 练习状态

所有数据记录在 `workspace/cpp-recovery/.practice-tracker/state.json`，包括：
- 当前进度（Day X / 70）
- 6维能力矩阵（syntax / pointer / memoryMgmt / template / design / concurrency）
- 每日提交记录、提示使用次数、校验得分
- 计划调整历史

### 运行测试，自检通过

```bash
export PATH="/c/msys64/ucrt64/bin:$PATH"
cmake --build build
./build/week01/day01_string_basic/day01_string_basic.exe
```

### 在进度表中勾选当天任务

打开本工作区内的 `PROGRESS.md`，在对应行打勾。`PROGRESS.md` 与 vault 中的计划笔记内容同步，单独打开工作区时无需切回 vault。

> 如需查看详细的题目描述、示例代码、业务映射，仍参考 vault 中的 `workspace/cpp-recovery/C++基础恢复70天计划.md`。

## 新增一天的目录

以 Day 2 为例：
```bash
mkdir -p week01/day02_string_copy_assign
```

然后新建 `week01/day02_string_copy_assign/CMakeLists.txt`：
```cmake
add_executable(day02_string_copy_assign main.cpp)
```

和 `week01/day02_string_copy_assign/main.cpp`，复制 Day 1 的模板开始写。

最后把 `week01/CMakeLists.txt` 里加上：
```cmake
add_subdirectory(day02_string_copy_assign)
```

重新 `cmake --build build` 即可。

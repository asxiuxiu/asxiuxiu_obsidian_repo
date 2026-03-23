# Claude 文档编写规则

## Skill 查找规则

查找 skill 时，**优先**在笔记工作区（vault）下的 `.claude/` 配置目录中查找，不要优先去用户目录（`~/.claude/`）下查找。查找顺序：

1. **Vault 级**：`<vault-root>/.claude/` 目录（最高优先级）
2. **用户级**：`~/.claude/` 目录（次级回退）

## 跨平台兼容规范

此 vault 会在 **Windows** 和 **macOS** 多个环境下使用，进行任何工作时须优先考虑跨平台兼容性：

### 代码工作区（workspace）
1. **统一使用 GCC（g++）**：GCC 在 Windows/macOS/Linux 均可安装，作为首选编译器
   - Windows：通过 [MSYS2](https://www.msys2.org/) 安装 `mingw-w64-ucrt-x86_64-gcc`，使用 `ucrt64` 环境
   - macOS：`brew install gcc` 或系统自带 `g++`（Apple Clang 别名，行为一致）
2. **CMake 生成器**：统一使用 `MinGW Makefiles`（Windows）或 `Unix Makefiles`（macOS），或两端都用 `Ninja`
   - 构建命令统一：`cmake -S . -B build -G "Ninja"` + `cmake --build build`
3. **路径分隔符**：CMakeLists.txt / Makefile 内使用正斜杠 `/`，避免 `\`
4. **Shell 脚本**：优先用 `cmake --build` / `ctest` 等跨平台命令，避免平台专属脚本
5. **换行符**：文本文件使用 LF（`.gitattributes` 控制），避免 CRLF 引发跨平台差异

### 文档 / 笔记
- 命令示例同时给出 macOS 和 Windows 版本，或使用跨平台写法（cmake、python 等）
- 路径示例用 `<vault-root>` 占位，不写死 `D:\...` 或 `/Users/...`

## 可视宽度规范

写文档时需考虑可视宽度：

1. **图表宽度控制**：如果画图（如 Mermaid 图表），尽量将宽度限制在合理的可视范围内（建议不超过 800-1000px 等效宽度）
2. **避免横向滚动**：确保内容在常规屏幕尺寸下无需横向滚动即可完整阅读
3. **替代方案**：当图表无法压缩到合理宽度时，考虑：
   - 将大图拆分为多个小图
   - 使用表格或列表替代复杂图表
   - 分层次展示（概要图 + 详细子图）
   - 文字描述补充

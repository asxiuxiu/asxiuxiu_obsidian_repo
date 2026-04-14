# Obsidian Run Code Plugin

在 Obsidian 中直接运行代码片段。

## 运行方式

- **JavaScript / TypeScript**：在 Obsidian 内部本地运行（基于 `new Function`），无需网络、无需 API Key，开箱即用。
- **Python / C++ / Java / Go / Rust 等**：默认通过 [Piston API](https://github.com/engineer-man/piston) 在线执行。
  - ⚠️ **注意**：Piston 公共 API 自 **2026年2月15日** 起需要 Authorization Token，不再免费开放。运行这些语言时若遇到 `401 Unauthorized`，说明公共 API 已受限。
  - 解决方案：在设置中切换到你自己部署的 Piston 私有实例，或仅使用 JS/TS 本地运行。

## 功能

- **阅读模式 & 编辑模式**：代码块左上角显示 ▶ 按钮，点击后在代码块下方输出运行结果。
- **设置面板**：
  - 自定义 Piston API 地址
  - 开关运行按钮显示
  - 自定义启用语言列表

## 安装

文件夹已位于 `.obsidian/plugins/obsidian-run-code/`，直接在 Obsidian **设置 → 社区插件** 中启用 **Run Code** 即可。

## 开发

```bash
npm install
npm run build
npm run dev
```

## 未来可能

- 集成 Pyodide（WebAssembly）实现 Python 本地离线运行。

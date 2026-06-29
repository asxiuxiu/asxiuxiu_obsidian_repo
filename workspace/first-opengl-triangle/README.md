# 第一个三角形（OpenGL 练习）

> 📝 源自笔记：[[Notes/计算机图形学/GPU编程基础/第一个三角形]]
> 📅 生成时间：2026-06-29

## 简介

这是 [[Notes/计算机图形学/GPU编程基础/第一个三角形]] 的配套练习工程。

你在笔记里看到的完整代码链路（GLFW 窗口 → GLAD 加载 → VAO/VBO → Shader → `glDrawArrays`）都在这里可以直接编译运行。

## 文件结构

```
first-opengl-triangle/
├── CMakeLists.txt          # 构建配置（自动下载 GLFW）
├── build.sh                # 一键构建脚本（处理 UCRT64 PATH）
├── README.md               # 本文件
├── include/
│   ├── glad/
│   │   └── glad.h          # OpenGL 函数指针加载器头文件
│   └── KHR/
│       └── khrplatform.h   # Khronos 平台类型定义
├── src/
│   ├── glad.c              # GLAD 实现
│   └── main.cpp            # 第一个三角形的完整代码
├── .vscode/
│   ├── launch.json         # VS Code 调试配置
│   └── tasks.json          # VS Code 构建任务
└── .zed/
    └── tasks.json          # Zed 构建任务
```

## 环境要求

- Windows + MSYS2 UCRT64 环境
- `g++`、`gcc`、`ninja`、`cmake`、`git` 已安装并可用
- 网络：首次构建会通过 `FetchContent` 从 GitHub 下载 GLFW 源码

## 快速开始

在 Git Bash 中执行：

```bash
cd workspace/first-opengl-triangle
bash build.sh
```

构建成功后运行：

```bash
./build/first-opengl-triangle.exe
```

按 `ESC` 退出窗口。

## 常见问题

### 编译器报错或 cc1 退出码 127

当前环境 PATH 中如果 `/mingw64/bin` 排在 `/ucrt64/bin` 前面，会导致 GCC 加载到错误版本的运行时 DLL。`build.sh` 会自动把 `/c/msys64/ucrt64/bin` 提到 PATH 最前面；若你手动运行 cmake，请先执行：

```bash
export PATH="/c/msys64/ucrt64/bin:${PATH}"
```

### GLFW 下载慢或失败

`CMakeLists.txt` 使用 `FetchContent` 在线拉取 GLFW。如果网络不通，可以手动把 GLFW 源码放到 `build/_deps/glfw-src/` 后重新配置。

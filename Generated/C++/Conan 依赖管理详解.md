---
title: Conan 依赖管理详解
date: 2026-03-23
tags:
  - cpp/package
  - conan
  - cmake
  - dependency-management
aliases:
  - C++包管理器
  - Conan教程
---

# Conan 依赖管理详解

> [!info] 官方资源
> - 官网：https://conan.io/
> - 文档：https://docs.conan.io/
> - GitHub：https://github.com/conan-io/conan

Conan 是 C/C++ 的依赖管理器和包管理器，类似于 JavaScript 的 npm、Python 的 pip、Rust 的 cargo。

---

## 核心概念

### 什么是 Conan？

```
┌─────────────────────────────────────────────────────────┐
│                    你的 C++ 项目                         │
│              (CMake/MSBuild/Makefile)                   │
├─────────────────────────────────────────────────────────┤
│  Conan ──► 管理第三方库（boost、openssl、zlib...）       │
│         ──► 处理版本冲突、传递依赖                        │
│         ──► 生成工具链文件供构建系统使用                   │
└─────────────────────────────────────────────────────────┘
```

### 版本演进

| 版本            | 状态     | 说明                  |
| ------------- | ------ | ------------------- |
| Conan 1.x     | 维护中    | 成熟稳定，大量项目仍在使用       |
| **Conan 2.x** | **推荐** | 现代 API，更好的性能，新项目的首选 |

> [!tip] 新项目建议直接使用 Conan 2，与老版本 API 不兼容。

---

## 安装与配置

### 安装方法

```bash
# pip 安装（推荐）
pip install conan

# 验证安装
conan --version

# 升级
pip install -U conan
```

### 初始配置

```bash
# 检测当前编译器并创建默认 profile
conan profile detect --force

# 查看生成的 profile
conan profile show
```

> [!note] Profile 是什么？
> Profile 描述了构建环境：操作系统、编译器、编译器版本、C++标准、构建类型（Debug/Release）等。

### 典型 Profile 内容

```ini
[settings]
arch=x86_64
build_type=Release
compiler=gcc
compiler.cppstd=gnu17
compiler.libcxx=libstdc++11
compiler.version=11
os=Linux

[conf]
tools.cmake.cmaketoolchain:generator=Ninja
```

---

## 核心工作流程

### 1. 声明依赖：`conanfile.txt`

最简单的依赖声明方式，适合纯消费者项目：

```ini
[requires]
zlib/1.2.13
boost/1.81.0
openssl/3.1.0

[generators]
CMakeDeps
CMakeToolchain

[options]
boost:shared=True
openssl:shared=False
```

| 区块 | 作用 |
|------|------|
| `[requires]` | 声明需要的包及其版本 |
| `[generators]` | 指定生成哪些构建系统集成文件 |
| `[options]` | 配置包的构建选项（如动态/静态）|

### 2. 声明依赖：`conanfile.py`（推荐）

更强大灵活的 Python 脚本形式：

```python
from conan import ConanFile
from conan.tools.cmake import cmake_layout, CMakeDeps, CMakeToolchain

class MyProject(ConanFile):
    name = "myproject"
    version = "1.0"
    settings = "os", "compiler", "build_type", "arch"

    # 依赖声明
    requires = [
        "zlib/1.2.13",
        "boost/1.81.0",
        "openssl/3.1.0",
    ]

    # 生成器
    generators = "CMakeDeps", "CMakeToolchain"

    # 可选配置
    default_options = {
        "boost/*:shared": True,
        "openssl/*:shared": False,
    }

    def layout(self):
        # 定义目录布局
        cmake_layout(self)

    def configure(self):
        # 配置阶段逻辑
        pass
```

### 3. 安装依赖

```bash
# 基本安装
conan install .

# 指定构建类型
conan install . -s build_type=Release
conan install . -s build_type=Debug

# 指定输出目录
conan install . -of build

# 从远程仓库安装
conan install . -r conancenter

# 构建缺失的包（如果二进制包不存在则源码编译）
conan install . --build=missing
```

### 4. 与 CMake 集成

```bash
# 步骤1：Conan 生成工具链
cd <project-root>
conan install . -of build --build=missing

# 步骤2：CMake 配置（使用 Conan 工具链）
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake

# 步骤3：构建
cmake --build build
```

> [!tip] 简化为单条命令
> `cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release`

---

## 生成器详解（Generators）

生成器负责创建与构建系统集成的文件。

### CMake 相关生成器

| 生成器 | 用途 | 输出文件 |
|--------|------|----------|
| `CMakeDeps` | 为每个依赖生成 `find_package` 支持 | `<Package>Config.cmake` |
| `CMakeToolchain` | 生成 CMake 工具链文件 | `conan_toolchain.cmake` |
| `CMakePresets` | 生成 CMake Presets | `CMakePresets.json` |

### 其他构建系统

| 生成器 | 适用构建系统 |
|--------|-------------|
| `MesonToolchain` | Meson |
| `MSBuildDeps` | Visual Studio MSBuild |
| `XcodeDeps` | Xcode |
| `MakeDeps` | Makefile |
| `PkgConfigDeps` | pkg-config |

### CMakeDeps 生成的文件

```
build/
├── conan_toolchain.cmake          # 工具链配置
├── CMakePresets.json              # CMake Presets
├── zlib-config.cmake              # zlib 的 CMake 配置
├── zlib-config-version.cmake
├── zlib-targets.cmake
├── boost-config.cmake             # boost 的 CMake 配置
└── ...
```

---

## 包仓库（Remotes）

### 官方仓库

```bash
# 查看配置的远程仓库
conan remote list

# 默认社区仓库
conancenter: https://center.conan.io [Verify SSL: True]

# 添加远程仓库
conan remote add myrepo https://my-repo.com/artifactory/api/conan/conan-local
```

### 搜索包

```bash
# 搜索官方仓库中的包
conan search zlib --remote=conancenter

# 查看包的可用版本
conan list zlib/* --remote=conancenter

# 查看包详情
conan inspect zlib/1.2.13
```

### 常用官方包

| 包名 | 说明 | 示例版本 |
|------|------|----------|
| `zlib` | 压缩库 | `zlib/1.2.13` |
| `openssl` | SSL/TLS 库 | `openssl/3.1.0` |
| `boost` | C++ 基础库 | `boost/1.81.0` |
| `fmt` | 格式化库 | `fmt/9.1.0` |
| `spdlog` | 日志库 | `spdlog/1.11.0` |
| `gtest` | 单元测试 | `gtest/1.13.0` |
| `benchmark` | 性能测试 | `benchmark/1.7.1` |
| `protobuf` | 序列化 | `protobuf/3.21.9` |
| `grpc` | RPC 框架 | `grpc/1.50.1` |
| `opencv` | 计算机视觉 | `opencv/4.7.0` |

---

## 版本管理

### 版本范围

```python
requires = [
    "zlib/[>=1.2.11 <2.0.0]",      # 版本范围
    "boost/[^1.80.0]",              # 兼容版本（允许 1.81.0, 1.82.0...）
    "openssl/3.1.0",                # 固定版本
    "fmt/*",                        # 最新版本
]
```

### 覆盖依赖（Overrides）

当依赖树中有版本冲突时：

```python
class MyProject(ConanFile):
    requires = [
        "A/1.0",           # A 依赖 C/1.0
        "B/1.0",           # B 依赖 C/2.0
    ]

    # 强制使用 C/2.0，所有依赖都使用这个版本
    overrides = ["C/2.0"]
```

### 锁定文件（Lockfiles）

锁定精确版本以确保可重现构建：

```bash
# 生成锁定文件
conan install . --lockfile-out=conan.lock

# 使用锁定文件安装（忽略版本范围，使用锁定版本）
conan install . --lockfile=conan.lock

# 更新锁定文件中的特定包
conan lock add --requires=boost/1.82.0
```

---

## 构建策略

### `--build` 选项

```bash
# 从不构建，只使用预编译二进制包
conan install . --build=never

# 构建缺失的包
conan install . --build=missing

# 强制重新构建所有依赖
conan install . --build="*"

# 仅构建特定包
conan install . --build=boost
conan install . --build=boost --build=openssl

# 级联构建（构建指定包及其依赖）
conan install . --build=cascade
```

### 配置选项（Options）

```bash
# 命令行设置选项
conan install . -o boost:shared=True
conan install . -o "*:shared=True"    # 设置所有包的共享库选项

# 多选项组合
conan install . -o zlib:shared=False -o boost:shared=True
```

常用选项：

| 选项 | 说明 | 值 |
|------|------|-----|
| `shared` | 动态/静态库 | `True`/`False` |
| `fPIC` | 位置无关代码 | `True`/`False` |
| `with_ssl` | SSL 支持 | `True`/`False` |
| `with_zlib` | zlib 压缩支持 | `True`/`False` |

---

## 高级用法

### 1. 创建自己的包

```python
# conanfile.py
from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMake, cmake_layout

class MyLibConan(ConanFile):
    name = "mylib"
    version = "1.0"
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}
    exports_sources = "CMakeLists.txt", "src/*", "include/*"

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["mylib"]
```

### 2. 构建并上传包

```bash
# 创建包
cd mylib
conan create . --user=myteam --channel=stable

# 上传包到远程仓库
conan upload mylib/1.0@myteam/stable -r myrepo

# 验证上传
conan list "mylib/*" -r myrepo
```

### 3. 使用 conan-center-index

Conan Center 的包源码在 GitHub 上维护：

```bash
# 克隆中心索引
git clone https://github.com/conan-io/conan-center-index.git

# 查看包的配方（recipe）
cd conan-center-index/recipes/zlib/all
```

### 4. 跨平台配置

```ini
# profiles/windows-msvc
[settings]
os=Windows
arch=x86_64
compiler=msvc
compiler.version=193
compiler.cppstd=17
build_type=Release

[conf]
tools.cmake.cmaketoolchain:generator=Visual Studio 17 2022

# profiles/linux-gcc
[settings]
os=Linux
arch=x86_64
compiler=gcc
compiler.version=11
compiler.libcxx=libstdc++11
compiler.cppstd=17
build_type=Release

[conf]
tools.cmake.cmaketoolchain:generator=Ninja
```

使用 profile：

```bash
conan install . -pr profiles/windows-msvc
conan install . -pr profiles/linux-gcc
```

---

## 完整项目示例

### 项目结构

```
myproject/
├── CMakeLists.txt
├── conanfile.py
├── src/
│   └── main.cpp
└── build/          # 生成目录
```

### conanfile.py

```python
from conan import ConanFile
from conan.tools.cmake import cmake_layout

class MyProject(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"

    def requirements(self):
        self.requires("fmt/9.1.0")
        self.requires("spdlog/1.11.0")

    def build_requirements(self):
        # 仅构建时需要的工具
        self.tool_requires("cmake/[>=3.23]")

    def layout(self):
        cmake_layout(self)
```

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.23)
project(MyProject CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Conan 生成的 find_package 支持
find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)

add_executable(myapp src/main.cpp)
target_link_libraries(myapp PRIVATE fmt::fmt spdlog::spdlog)
```

### main.cpp

```cpp
#include <spdlog/spdlog.h>
#include <fmt/format.h>

int main() {
    spdlog::info("Hello from spdlog!");
    auto message = fmt::format("2 + 2 = {}", 2 + 2);
    spdlog::info(message);
    return 0;
}
```

### 构建脚本

```bash
#!/bin/bash
set -e

# 安装依赖
conan install . --output-folder=build --build=missing

# 配置
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake

# 构建
cmake --build build --config Release

# 运行
./build/myapp
```

---

## 常见问题

### Q: 如何处理找不到的二进制包？

```bash
# 从源码构建缺失的包
conan install . --build=missing

# 强制重新构建特定包
conan install . --build=boost
```

### Q: 如何清理 Conan 缓存？

```bash
# 查看缓存位置
conan config home

# 清理所有包缓存
conan remove "*" -c

# 清理特定包
conan remove "boost/*" -c

# 清理未使用的包
conan cache clean
```

### Q: 如何处理 "Package not found"？

```bash
# 确保远程仓库配置正确
conan remote list

# 添加官方仓库
conan remote add conancenter https://center.conan.io

# 搜索包
conan search package_name -r conancenter
```

### Q: 不同项目使用不同版本的同一个库？

Conan 2 支持版本隔离，不同项目可以独立管理依赖版本。

### Q: 如何在 CI/CD 中使用？

```yaml
# .github/workflows/build.yml 示例
- name: Install Conan
  run: pip install conan

- name: Configure Conan
  run: conan profile detect --force

- name: Install dependencies
  run: conan install . --build=missing

- name: Build
  run: |
    cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake
    cmake --build build
```

---

## 与引擎项目的对比

| 特性 | 引擎项目 | 标准 Conan 用法 |
|------|---------|----------------|
| Conanfile | `conanfile.py` 声明依赖 | 同上 |
| 生成器 | `CMakeDeps` + `CMakeToolchain` | 同上 |
| 工具链 | `conan_toolchain.cmake` | 同上 |
| 额外步骤 | `chaos_launch_generator.exe` 生成源码树 | 无 |
| 部署 | 自定义 `--deployer-folder` | 使用 `deploy` 生成器或直接链接 |

> [!note] 引擎的特殊之处
> 引擎项目使用 `chaos_launch_generator` 在 CMake 配置前生成源码树（sourcetree），这是引擎特有的项目组织方式，不属于标准 Conan 工作流。

---

## 参考链接

- [Conan 2.0 官方文档](https://docs.conan.io/2/)
- [ConanCenter 包浏览器](https://conan.io/center)
- [CMake 集成指南](https://docs.conan.io/2/examples/tools/cmake/cmake_toolchain/build_project_cmake_presets.html)
- [Conan GitHub](https://github.com/conan-io/conan)
- [创建包的教程](https://docs.conan.io/2/tutorial/creating_packages.html)

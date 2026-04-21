---
title: Shader 编译流程
description: 解析 Piccolo 的 Shader 编译管线，从 GLSL 到 SPIR-V 再到内嵌 C++ Header 的完整链路。
date: 2026-04-12
tags:
  - piccolo
  - source-analysis
  - build-system
  - vulkan
  - shader
aliases:
  - Piccolo-Shader-编译流程
---

> [[Notes/Piccolo/索引|← 返回 Piccolo 索引]]

# 构建系统-源码解析：Shader 编译流程

## Why：为什么要学习 Piccolo 的 Shader 编译流程？

- **问题背景**：Vulkan 使用 SPIR-V 作为中间字节码，但开发者通常用 GLSL 编写 Shader。引擎需要在构建阶段完成 GLSL → SPIR-V 的编译，并将字节码打包到可执行文件中，避免运行时读取外部文件。
- **不用它的后果**：运行时依赖文件系统读取 `.spv` 文件，导致发布包体积膨胀、加载逻辑复杂、资源泄露风险增加；GLSL 更新后需要手动调用 glslangValidator 编译。
- **应用场景**：
  1. 自研引擎中实现自动 Shader 编译与内嵌资源管理。
  2. 理解 Vulkan 项目的标准资源构建管线。
  3. 为其他资源类型（如贴图、模型）设计类似的构建时嵌入流程。

## What：Piccolo 的 Shader 编译流程是什么？

Piccolo 的 Shader 构建管线是 CMake 驱动的一条**双阶段代码生成链**：

```
GLSL 源文件
    ↓  glslangValidator
SPIR-V 二进制文件 (.spv)
    ↓  CMake 脚本 (GenerateShaderCPPFile.cmake)
C++ Header 文件 (.h)
    ↓  被 PiccoloRuntime 编译链接
最终可执行文件（Shader 字节码内嵌）
```

```mermaid
graph LR
    A[GLSL Files] -->|glslangValidator| B[.spv Files]
    B -->|embed_resource| C[C++ Headers]
    C --> D[PiccoloRuntime]
    D --> E[PiccoloEditor]
```

## How：Piccolo 是如何实现的？

### 1. Shader 目录组织

`engine/shader/` 目录下：

- `glsl/`：存放所有 GLSL 源文件（`.vert`、`.frag`、`.comp`、`.geom` 等）
- `include/`：存放 Shader 公共头文件（`constants.h`、`structures.h`、`gbuffer.h` 等）
- `generated/cpp/`：自动生成的 C++ Header（每个 Shader 一个 `.h`）
- `generated/spv/`：自动生成的 SPIR-V 二进制文件

### 2. engine/shader/CMakeLists.txt：目标定义

> 文件：`engine/shader/CMakeLists.txt`，第 1~38 行

```cpp
set(TARGET_NAME ${SHADER_COMPILE_TARGET})  // PiccoloShaderCompile

file(GLOB_RECURSE SHADER_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/glsl/*.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/glsl/*.frag"
    "${CMAKE_CURRENT_SOURCE_DIR}/glsl/*.comp"
    ... // 其他 Shader 扩展名
)

set(SHADER_INCLUDE_FOLDER ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(GENERATED_SHADER_FOLDER "generated")

include(${PICCOLO_ROOT_DIR}/cmake/ShaderCompile.cmake)

compile_shader(
  "${SHADER_FILES}"
  "${TARGET_NAME}"
  "${SHADER_INCLUDE_FOLDER}"
  "${GENERATED_SHADER_FOLDER}"
  "${glslangValidator_executable}")

set_target_properties("${TARGET_NAME}" PROPERTIES FOLDER "Engine")
```

这里定义了 `PiccoloShaderCompile` 自定义目标，负责驱动所有 Shader 的编译。`CONFIGURE_DEPENDS` 让 CMake 在 GLSL 文件新增时自动重新配置。

### 3. cmake/ShaderCompile.cmake：批量编译逻辑

> 文件：`cmake/ShaderCompile.cmake`，第 1~43 行

```cpp
function(compile_shader SHADERS TARGET_NAME SHADER_INCLUDE_FOLDER GENERATED_DIR GLSLANG_BIN)
    set(working_dir "${CMAKE_CURRENT_SOURCE_DIR}")
    set(ALL_GENERATED_SPV_FILES "")
    set(ALL_GENERATED_CPP_FILES "")

    foreach(SHADER ${SHADERS})
        get_filename_component(SHADER_NAME ${SHADER} NAME)
        string(REPLACE "." "_" HEADER_NAME ${SHADER_NAME})
        string(TOUPPER ${HEADER_NAME} GLOBAL_SHADER_VAR)

        set(SPV_FILE "${CMAKE_CURRENT_SOURCE_DIR}/${GENERATED_DIR}/spv/${SHADER_NAME}.spv")
        set(CPP_FILE "${CMAKE_CURRENT_SOURCE_DIR}/${GENERATED_DIR}/cpp/${HEADER_NAME}.h")

        add_custom_command(
            OUTPUT ${SPV_FILE}
            COMMAND ${GLSLANG_BIN} -I${SHADER_INCLUDE_FOLDER} -V100 -o ${SPV_FILE} ${SHADER}
            DEPENDS ${SHADER}
            WORKING_DIRECTORY "${working_dir}")

        add_custom_command(
            OUTPUT ${CPP_FILE}
            COMMAND ${CMAKE_COMMAND} -DPATH=${SPV_FILE} -DHEADER="${CPP_FILE}"
                -DGLOBAL="${GLOBAL_SHADER_VAR}"
                -P "${PICCOLO_ROOT_DIR}/cmake/GenerateShaderCPPFile.cmake"
            DEPENDS ${SPV_FILE}
            WORKING_DIRECTORY "${working_dir}")

        list(APPEND ALL_GENERATED_SPV_FILES ${SPV_FILE})
        list(APPEND ALL_GENERATED_CPP_FILES ${CPP_FILE})
    endforeach()

    add_custom_target(${TARGET_NAME}
        DEPENDS ${ALL_GENERATED_SPV_FILES} ${ALL_GENERATED_CPP_FILES} SOURCES ${SHADERS})
endfunction()
```

对**每一个 GLSL 文件**，`compile_shader` 函数会创建两条 `add_custom_command`：

1. **GLSL → SPIR-V**：调用 `glslangValidator`，参数 `-V100` 表示生成 SPIR-V v1.0，`-I` 指定 include 目录。
2. **SPIR-V → C++ Header**：调用 `GenerateShaderCPPFile.cmake` 脚本，将 `.spv` 二进制文件转换为一个 `static const std::vector<unsigned char>` 变量。

最后，用 `add_custom_target` 汇总所有生成的文件，形成一个可被其他目标依赖的 `PiccoloShaderCompile` 目标。

### 4. cmake/GenerateShaderCPPFile.cmake：资源内嵌

> 文件：`cmake/GenerateShaderCPPFile.cmake`，第 10~40 行

```cpp
function(embed_resource resource_file_name source_file_name variable_name)
    file(READ "${resource_file_name}" hex_content HEX)

    string(REPEAT "[0-9a-f]" 32 pattern)
    string(REGEX REPLACE "(${pattern})" "\\1\n" content "${hex_content}")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1, " content "${content}")
    string(REGEX REPLACE ", $" "" content "${content}")

    set(array_definition "static const std::vector<unsigned char> ${variable_name} =\n{\n${content}\n};")
    set(source "/**\n * @file ...\n * @brief Auto generated file.\n */\n#include <vector>\n${array_definition}\n")

    file(WRITE "${source_file_name}" "${source}")
endfunction()

embed_resource("${PATH}" "${HEADER}" "${GLOBAL}")
```

这段 CMake 脚本的核心逻辑是：
1. 以 **HEX** 模式读取 `.spv` 二进制文件；
2. 将每两个十六进制字符转换为 `0xFF, ` 格式的 C++ 数组元素；
3. 包装成 `std::vector<unsigned char>` 变量；
4. 写入 `.h` 文件。

生成的文件示例（`engine/shader/generated/cpp/axis_vert.h`）：

```cpp
#include <vector>
static const std::vector<unsigned char> AXIS_VERT =
{
0x03, 0x02, 0x23, 0x07, 0x00, 0x00, 0x01, 0x00, ...
};
```

### 5. Runtime 中的使用方式

`engine/source/runtime/CMakeLists.txt` 将生成目录加入 include 路径：

```cpp
target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${ENGINE_ROOT_DIR}/shader/generated/cpp>)
```

因此，渲染层的各个 Pass 可以直接用短路径 include：

> 文件：`engine/source/runtime/function/render/passes/main_camera_pass.cpp`，第 12~20 行

```cpp
#include <axis_frag.h>
#include <axis_vert.h>
#include <deferred_lighting_frag.h>
#include <deferred_lighting_vert.h>
#include <mesh_frag.h>
#include <mesh_gbuffer_frag.h>
#include <mesh_vert.h>
#include <skybox_frag.h>
#include <skybox_vert.h>
```

在运行时，这些变量（如 `AXIS_VERT`）就是普通的 `std::vector<unsigned char>`，可以直接传给 `vkCreateShaderModule` 创建 Vulkan Shader Module。

## 与上下层的关系

- **上层调用者**：`PiccoloShaderCompile` 被 `PiccoloRuntime` 通过 `add_dependencies(PiccoloRuntime ${SHADER_COMPILE_TARGET})` 依赖。这意味着在编译 Runtime 之前，所有 Shader 必须已经生成完毕。
- **下层依赖**：
  - `glslangValidator`（Vulkan SDK 自带）负责 SPIR-V 编译；
  - `GenerateShaderCPPFile.cmake` 是纯 CMake 脚本，无外部依赖；
  - 渲染层的各个 Pass（如 `main_camera_pass`、`fxaa_pass`）是生成代码的消费方。

## 设计亮点与可迁移原理

1. **CMake `add_custom_command` + `add_custom_target` 的精确依赖链**
   - 每个 `.spv` 只依赖对应的 `.glsl`；每个 `.h` 只依赖对应的 `.spv`。这种细粒度依赖让 Ninja/Make 可以**增量编译**：修改一个 Shader 只会触发该 Shader 的重编译，而不是全部。
   - **可迁移点**：自研引擎的代码生成或资源编译管线，也应避免"一改动就全量重建"。为每个输入文件单独定义 `add_custom_command` 是最佳实践。

2. **Shader 字节码直接内嵌到可执行文件**
   - 将 `.spv` 转换为 C++ 数组后，Shader 成为了编译产物的一部分。运行时无需文件 I/O、无需资源打包系统，启动更快，发布更简单。
   - **可迁移点**：对于固定管线或内置 Shader 较多的引擎，内嵌 Shader 能显著简化资源管理。但要注意，这会增大可执行文件体积，且不适合需要频繁热更 Shader 的场景。

3. **include 路径的短路径设计**
   - `engine/shader/generated/cpp` 被直接加入 include path，因此代码中可以写 `#include <axis_vert.h>` 而不是 `#include <engine/shader/generated/cpp/axis_vert.h>`。这种短路径让渲染代码更聚焦于逻辑而非物理路径。
   - **可迁移点**：为生成的代码目录提供独立的、扁平的 include 路径，可以提升消费代码的可读性，但要避免命名冲突。

## 关键源码片段

> 文件：`cmake/ShaderCompile.cmake`，第 21~34 行

```cpp
add_custom_command(
    OUTPUT ${SPV_FILE}
    COMMAND ${GLSLANG_BIN} -I${SHADER_INCLUDE_FOLDER} -V100 -o ${SPV_FILE} ${SHADER}
    DEPENDS ${SHADER}
    WORKING_DIRECTORY "${working_dir}")

add_custom_command(
    OUTPUT ${CPP_FILE}
    COMMAND ${CMAKE_COMMAND} -DPATH=${SPV_FILE} -DHEADER="${CPP_FILE}"
        -DGLOBAL="${GLOBAL_SHADER_VAR}"
        -P "${PICCOLO_ROOT_DIR}/cmake/GenerateShaderCPPFile.cmake"
    DEPENDS ${SPV_FILE}
    WORKING_DIRECTORY "${working_dir}")
```

## 关联阅读

- [[构建系统-源码解析：CMake 顶层架构与模块组织|CMake 顶层架构与模块组织]]
- [[渲染层-源码解析：Vulkan 接口封装|Vulkan 接口封装]]
- [[渲染层-源码解析：主相机 Pass 与光照|主相机 Pass 与光照]]

---

**索引状态**：第一轮（接口层/骨架扫描）已完成。

#!/usr/bin/env bash
set -euo pipefail

# 确保使用 UCRT64 工具链，避免与 mingw64 的 DLL 混用导致编译/运行异常
if [[ -d "/c/msys64/ucrt64/bin" ]]; then
    export PATH="/c/msys64/ucrt64/bin:${PATH}"
fi

cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build

# 将编译数据库复制到项目根目录，方便 clangd 自动加载
if [[ -f build/compile_commands.json ]]; then
    cp -f build/compile_commands.json .
fi

echo ""
echo "构建完成，可执行文件：build/first-opengl-triangle.exe"

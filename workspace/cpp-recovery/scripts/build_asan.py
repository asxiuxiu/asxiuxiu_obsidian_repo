#!/usr/bin/env python3
"""
一键 Configure + Build ASan 版本。
Zed 任务调用此脚本，避免 shell 兼容性问题。
"""
import os
import subprocess
import sys

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD_DIR = os.path.join(PROJECT_ROOT, "build-asan")

# 确保 ucrt64/bin 在 PATH 最前面（解决 MinGW DLL 冲突）
ucrt64 = r"C:\msys64\ucrt64\bin"
path_parts = os.environ.get("PATH", "").split(os.pathsep)
# 去重，把 ucrt64 放到最前面
path_parts = [p for p in path_parts if p.lower() != ucrt64.lower()]
path_parts.insert(0, ucrt64)
env = os.environ.copy()
env["PATH"] = os.pathsep.join(path_parts)

cmd_configure = [
    "cmake",
    "-S", PROJECT_ROOT,
    "-B", BUILD_DIR,
    "-G", "Ninja",
    "-DCMAKE_CXX_COMPILER=g++",
    "-DENABLE_ASAN=ON",
]

cmd_build = [
    "cmake",
    "--build", BUILD_DIR,
]

print("[ASan] Configuring...")
result = subprocess.run(cmd_configure, env=env)
if result.returncode != 0:
    print("[ASan] Configure failed.", file=sys.stderr)
    sys.exit(result.returncode)

print("[ASan] Building...")
result = subprocess.run(cmd_build, env=env)
if result.returncode != 0:
    print("[ASan] Build failed.", file=sys.stderr)
    sys.exit(result.returncode)

print("[ASan] Done.")

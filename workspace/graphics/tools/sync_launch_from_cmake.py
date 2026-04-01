#!/usr/bin/env python3
"""
根据根 CMakeLists.txt 的 add_subdirectory 与各子目录中的 add_executable，
生成/更新 .vscode/launch.json 中的 cppdbg 配置。

约定：
- 每个 demo 一个子目录，子目录内有 CMakeLists.txt，且含 add_executable(<target> ...)。
- 可执行文件路径：build/<subdir>/<target>(.exe)，与 Ninja + 常用 add_subdirectory 布局一致。

同名「Debug <target>」的配置每次同步会被覆盖；其余 launch 配置保留。
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path


def strip_cmake_comments(text: str) -> str:
    out_lines: list[str] = []
    for line in text.splitlines():
        if "#" in line:
            line = line.split("#", 1)[0]
        out_lines.append(line)
    return "\n".join(out_lines)


def find_add_subdirectory_args(cmake_text: str) -> list[str]:
    text = strip_cmake_comments(cmake_text)
    paths: list[str] = []
    for m in re.finditer(r"add_subdirectory\s*\(\s*([^)]+)\)", text, flags=re.I):
        inner = m.group(1).strip()
        parts = inner.split()
        if not parts:
            continue
        raw = parts[0].strip().strip('"')
        if "${" in raw:
            continue
        paths.append(raw.replace("\\", "/"))
    return paths


def first_add_executable_target(cmake_path: Path) -> str | None:
    if not cmake_path.is_file():
        return None
    text = strip_cmake_comments(cmake_path.read_text(encoding="utf-8"))
    m = re.search(r"add_executable\s*\(\s*([^\s\)]+)", text, flags=re.I)
    if not m:
        return None
    name = m.group(1).strip()
    if name.startswith("${"):
        return None
    return name


def make_launch_config(subdir: str, target: str) -> dict:
    rel = subdir.replace("\\", "/")
    name = f"Debug {target}"
    return {
        "name": name,
        "type": "cppdbg",
        "request": "launch",
        "program": f"${{workspaceFolder}}/build/{rel}/{target}",
        "args": [],
        "stopAtEntry": False,
        "cwd": "${workspaceFolder}",
        "environment": [],
        "externalConsole": False,
        "MIMode": "gdb",
        "miDebuggerPath": "gdb",
        "setupCommands": [
            {
                "description": "为 gdb 启用整齐打印",
                "text": "-enable-pretty-printing",
                "ignoreFailures": True,
            }
        ],
        "preLaunchTask": "CMake: Build",
        "windows": {
            "program": f"${{workspaceFolder}}/build/{rel}/{target}.exe",
            "miDebuggerPath": "gdb.exe",
        },
    }


def main() -> int:
    root = Path.cwd()
    root_cmake = root / "CMakeLists.txt"
    launch_path = root / ".vscode" / "launch.json"

    if not root_cmake.is_file():
        print("错误：当前目录下没有 CMakeLists.txt（请在 graphics 根目录运行）", file=sys.stderr)
        return 1

    subdirs = find_add_subdirectory_args(root_cmake.read_text(encoding="utf-8"))
    if not subdirs:
        print("错误：根 CMakeLists.txt 中未发现 add_subdirectory", file=sys.stderr)
        return 1

    generated: list[dict] = []
    targets_seen: set[str] = set()
    for sub in subdirs:
        sub_cmake = root / sub / "CMakeLists.txt"
        target = first_add_executable_target(sub_cmake)
        if not target:
            print(f"跳过「{sub}」：未找到 add_executable", file=sys.stderr)
            continue
        if target in targets_seen:
            print(
                f"错误：目标名「{target}」重复，请让每个 add_executable 名字唯一",
                file=sys.stderr,
            )
            return 1
        targets_seen.add(target)
        generated.append(make_launch_config(sub, target))

    if not generated:
        print("错误：没有可用的 demo 配置", file=sys.stderr)
        return 1

    gen_names = {c["name"] for c in generated}
    manual: list[dict] = []
    if launch_path.is_file():
        try:
            data = json.loads(launch_path.read_text(encoding="utf-8"))
            for cfg in data.get("configurations", []):
                if cfg.get("name") not in gen_names:
                    manual.append(cfg)
        except json.JSONDecodeError as e:
            print(f"错误：无法解析 {launch_path}: {e}", file=sys.stderr)
            return 1

    generated.sort(key=lambda c: c["name"])
    out = {
        "version": "0.2.0",
        "configurations": manual + generated,
    }

    launch_path.parent.mkdir(parents=True, exist_ok=True)
    launch_path.write_text(
        json.dumps(out, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(
        f"已更新 {launch_path}：自动生成 {len(generated)} 个，"
        f"保留手动配置 {len(manual)} 个。"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

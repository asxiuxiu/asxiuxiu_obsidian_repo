#!/usr/bin/env python3
"""
一键构建并运行单个 demo。

用法：
    python tools/run_demo.py <demo-name>

示例：
    python tools/run_demo.py pixel-demo
    python tools/run_demo.py rasterization

脚本会自动：
1. 从根 CMakeLists.txt 发现所有 demo 子目录。
2. 若未配置则执行 cmake configure。
3. 仅构建指定的 demo target。
4. 运行产物，PPM 等输出会落在 build/<demo-name>/ 下。
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path


def strip_cmake_comments(text: str) -> str:
    out_lines: list[str] = []
    for line in text.splitlines():
        if "#" in line:
            line = line.split("#", 1)[0]
        out_lines.append(line)
    return "\n".join(out_lines)


def find_demos(root: Path) -> list[str]:
    cmake_path = root / "CMakeLists.txt"
    if not cmake_path.is_file():
        raise FileNotFoundError(f"未找到根 CMakeLists.txt: {cmake_path}")

    text = strip_cmake_comments(cmake_path.read_text(encoding="utf-8"))
    demos: list[str] = []
    for m in re.finditer(r"add_subdirectory\s*\(\s*([^)]+)\)", text, flags=re.I):
        inner = m.group(1).strip()
        parts = inner.split()
        if not parts:
            continue
        raw = parts[0].strip().strip('"')
        if "${" in raw:
            continue
        demos.append(raw.replace("\\", "/"))
    return demos


def discover_targets(root: Path, demos: list[str]) -> dict[str, str]:
    """返回 {target_name: subdir}，要求每个子目录的第一个 add_executable 名字唯一。"""
    targets: dict[str, str] = {}
    for sub in demos:
        cmake_path = root / sub / "CMakeLists.txt"
        if not cmake_path.is_file():
            continue
        text = strip_cmake_comments(cmake_path.read_text(encoding="utf-8"))
        m = re.search(r"add_executable\s*\(\s*([^\s\)]+)", text, flags=re.I)
        if not m:
            continue
        target = m.group(1).strip()
        if target.startswith("${"):
            continue
        if target in targets:
            raise ValueError(f"目标名重复: {target}")
        targets[target] = sub
    return targets


def run(
    cmd: list[str], cwd: Path, *, check: bool = True
) -> subprocess.CompletedProcess:
    print(f"\n> > > {' '.join(cmd)}")
    return subprocess.run(cmd, cwd=cwd, check=check)


def main() -> int:
    parser = argparse.ArgumentParser(description="构建并运行单个 graphics demo")
    parser.add_argument("demo", help="demo 目标名，例如 pixel-demo 或 rasterization")
    parser.add_argument(
        "--build-dir",
        default="build",
        help="CMake 构建目录（默认: build）",
    )
    parser.add_argument(
        "--config",
        default="Debug",
        help="CMake 构建类型（默认: Debug）",
    )
    parser.add_argument(
        "--generator",
        default="Ninja",
        help="CMake generator（默认: Ninja）",
    )
    args = parser.parse_args()

    root = Path.cwd()
    demos = find_demos(root)
    if not demos:
        print("错误：根 CMakeLists.txt 中未发现 demo", file=sys.stderr)
        return 1

    targets = discover_targets(root, demos)
    if args.demo not in targets:
        print(
            f"错误：未找到 demo「{args.demo}」。可用的 demo: {', '.join(sorted(targets))}",
            file=sys.stderr,
        )
        return 1

    build_dir = root / args.build_dir
    if not (build_dir / "CMakeCache.txt").is_file():
        run(
            [
                "cmake",
                "-B",
                str(build_dir),
                "-S",
                ".",
                "-G",
                args.generator,
                f"-DCMAKE_BUILD_TYPE={args.config}",
            ],
            cwd=root,
        )

    run(
        [
            "cmake",
            "--build",
            str(build_dir),
            "--target",
            args.demo,
            "--config",
            args.config,
            "--parallel",
        ],
        cwd=root,
    )

    exe_suffix = ".exe" if sys.platform == "win32" else ""
    exe_path = build_dir / args.demo / f"{args.demo}{exe_suffix}"
    if not exe_path.is_file():
        print(f"错误：未找到可执行文件 {exe_path}", file=sys.stderr)
        return 1

    run([str(exe_path)], cwd=build_dir / args.demo)

    print(f"\n✓ demo「{args.demo}」运行完成，产物在 {build_dir / args.demo}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

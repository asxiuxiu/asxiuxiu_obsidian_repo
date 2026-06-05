#!/usr/bin/env python3
"""
根据当前打开的文件，自动找到并运行对应的练习可执行文件。
用法: python scripts/run_current.py <当前文件路径> [工作区根目录]
"""

import os
import sys
import subprocess


def main():
    args = sys.argv[1:]
    do_build = "--build" in args
    if do_build:
        args.remove("--build")

    if len(args) < 1:
        print("Usage: python scripts/run_current.py [--build] <current_file> [worktree_root]")
        sys.exit(1)

    current_path = args[0]
    worktree_root = args[1] if len(args) > 1 else os.environ.get("ZED_WORKTREE_ROOT", os.getcwd())
    worktree_root = os.path.abspath(worktree_root)

    if os.path.isfile(current_path):
        exercise_dir = os.path.dirname(current_path)
    else:
        exercise_dir = current_path

    exercise_dir = os.path.abspath(exercise_dir)

    try:
        rel_dir = os.path.relpath(exercise_dir, worktree_root)
    except ValueError:
        print(f"[Error] Directory not inside worktree: {exercise_dir}")
        sys.exit(1)

    # Walk up to find the exercise root (directory containing main.cpp)
    search_dir = exercise_dir
    while search_dir != worktree_root and search_dir != os.path.dirname(search_dir):
        if os.path.exists(os.path.join(search_dir, "main.cpp")):
            exercise_dir = search_dir
            rel_dir = os.path.relpath(search_dir, worktree_root)
            break
        search_dir = os.path.dirname(search_dir)

    basename = os.path.basename(exercise_dir)
    exe_name = f"{basename}.exe"
    exe_path = os.path.join(worktree_root, "build", rel_dir, exe_name)
    exe_path = os.path.normpath(exe_path)

    if do_build:
        print("[Build] Building...")
        build_result = subprocess.run(
            ["cmake", "--build", "build"],
            cwd=worktree_root
        )
        if build_result.returncode != 0:
            print("[Build] Build failed")
            sys.exit(build_result.returncode)
        print("[Build] Done\n")

    if not os.path.exists(exe_path):
        print(f"[Error] Executable not found: {exe_path}")
        print("        Run [Build] first")
        sys.exit(1)

    print(f"[Run] {exe_path}")
    print("-" * 40)

    # 在 exe 所在目录运行，避免相对路径问题
    result = subprocess.run([exe_path], cwd=os.path.dirname(exe_path))
    sys.exit(result.returncode)


if __name__ == "__main__":
    main()

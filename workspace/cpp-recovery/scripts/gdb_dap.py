#!/usr/bin/env python3
"""
DAP proxy for Zed C++ debugging.
Intercepts launch requests to auto-resolve executable paths from source files.

Usage: python scripts/gdb_dap.py
  Reads DAP messages from stdin, forwards to gdb --interpreter=dap,
  automatically replacing .cpp/.h program paths with their build executables.
"""

import json
import os
import subprocess
import sys
import threading


def find_exe(program_path):
    """Resolve a source file path to its corresponding executable."""
    if os.path.isfile(program_path):
        exercise_dir = os.path.dirname(program_path)
    else:
        exercise_dir = program_path

    exercise_dir = os.path.abspath(exercise_dir)

    # Find worktree root by looking for a build/ directory
    worktree_root = exercise_dir
    while worktree_root != os.path.dirname(worktree_root):
        if os.path.isdir(os.path.join(worktree_root, "build")):
            break
        worktree_root = os.path.dirname(worktree_root)

    # Walk up to find exercise root (directory containing main.cpp)
    search_dir = exercise_dir
    while search_dir != worktree_root and search_dir != os.path.dirname(search_dir):
        if os.path.exists(os.path.join(search_dir, "main.cpp")):
            exercise_dir = search_dir
            break
        search_dir = os.path.dirname(search_dir)

    rel_dir = os.path.relpath(exercise_dir, worktree_root)
    basename = os.path.basename(exercise_dir)
    exe_path = os.path.join(worktree_root, "build", rel_dir, f"{basename}.exe")
    return os.path.normpath(exe_path)


def read_dap_msg(stream):
    """Read one DAP message from a binary stream."""
    header = b""
    while True:
        line = stream.readline()
        if not line:
            return None
        if line == b"\r\n":
            break
        header += line

    content_length = 0
    for h in header.decode("utf-8", errors="replace").split("\r\n"):
        if h.startswith("Content-Length:"):
            try:
                content_length = int(h.split(":", 1)[1].strip())
            except ValueError:
                pass
            break

    if content_length <= 0:
        return None

    data = stream.read(content_length)
    if len(data) < content_length:
        return None
    return json.loads(data.decode("utf-8", errors="replace"))


def write_dap_msg(stream, msg):
    """Write one DAP message to a binary stream."""
    data = json.dumps(msg).encode("utf-8")
    header = f"Content-Length: {len(data)}\r\n\r\n".encode("utf-8")
    stream.write(header + data)
    stream.flush()


def forward(src, dst, modify=None):
    """Forward DAP messages from src to dst, optionally modifying them."""
    while True:
        msg = read_dap_msg(src)
        if msg is None:
            break
        if modify:
            msg = modify(msg)
        write_dap_msg(dst, msg)


def modify_launch(msg):
    """Intercept launch requests to fix program path."""
    if msg.get("type") == "request" and msg.get("command") == "launch":
        args = msg.get("arguments", {})
        program = args.get("program", "")
        if program and any(
            program.endswith(ext) for ext in (".cpp", ".cc", ".c", ".h", ".hpp")
        ):
            exe = find_exe(program)
            if os.path.exists(exe):
                args["program"] = exe
                args["cwd"] = os.path.dirname(exe)
                sys.stderr.write(f"[gdb_dap] Resolved: {program} -> {exe}\n")
            else:
                sys.stderr.write(f"[gdb_dap] Executable not found: {exe}\n")
            sys.stderr.flush()
    return msg


def main():
    gdb = subprocess.Popen(
        ["gdb", "--interpreter=dap"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=sys.stderr,
    )

    t1 = threading.Thread(
        target=forward, args=(sys.stdin.buffer, gdb.stdin, modify_launch)
    )
    t2 = threading.Thread(target=forward, args=(gdb.stdout, sys.stdout.buffer, None))

    t1.start()
    t2.start()
    t1.join()
    t2.join()
    gdb.wait()


if __name__ == "__main__":
    main()

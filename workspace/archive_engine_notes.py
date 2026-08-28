#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""归档 计算机图形学 + SelfGameEngine 两个目录到 archive/*.zip（只读，不删源）。
排除 .trash 废弃草稿。"""
import os
import sys
import zipfile
from datetime import datetime

VAULT = r"D:/asxiuxiu_obsidian_repo"
NOTES = os.path.join(VAULT, "Notes")
ARCHIVE_DIR = os.path.join(VAULT, "archive")

archive_dirs = [
    "计算机图形学",
    "SelfGameEngine",
]

def main():
    os.makedirs(ARCHIVE_DIR, exist_ok=True)
    stamp = datetime.now().strftime("%Y-%m-%d")
    out = os.path.join(ARCHIVE_DIR, f"graphics-rendering-archive-{stamp}.zip")
    files = {}
    for d in archive_dirs:
        base = os.path.join(NOTES, d)
        if not os.path.isdir(base):
            print(f"[警告] 目录不存在，跳过: {base}")
            continue
        for root, dirs, names in os.walk(base):
            if ".trash" in dirs:
                dirs.remove(".trash")
            for n in names:
                if n.endswith(".md"):
                    full = os.path.join(root, n)
                    rel = os.path.relpath(full, NOTES)
                    files[os.path.join("Notes", rel).replace("\\", "/")] = full
    if not files:
        print("没有收集到文件", file=sys.stderr)
        sys.exit(1)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zf:
        for arcname, full in sorted(files.items()):
            zf.write(full, arcname)
    # 按顶层统计
    from collections import Counter
    c = Counter()
    for k in files:
        c[k.split("/")[1]] += 1
    print(f"归档完成: {out}")
    print(f"总篇数: {len(files)}")
    for k, v in c.items():
        print(f"  {k}: {v}")

if __name__ == "__main__":
    main()

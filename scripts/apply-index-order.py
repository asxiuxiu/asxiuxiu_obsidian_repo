#!/usr/bin/env python3
"""
根据索引文件中的 wikilink 顺序，为对应笔记注入 frontmatter `order`。
用于 Quartz 站点生成时保持侧边栏/文件夹页与索引顺序一致。

用法：
    python scripts/apply-index-order.py Notes/SelfGameEngine/0_RoadMap.md

行为：
    1. 读取索引文件，按首次出现顺序提取所有指向外部笔记的 wikilink；
    2. 在索引文件所在目录下递归查找对应 .md 文件；
    3. 为每个匹配到的笔记写入 `order: N`（从 1 开始）；
    4. 索引文件本身获得 `order: 0`，确保排在最前；
    5. 已存在 order 的笔记会被更新，无需更新的文件不会重写。

注意：
    - 只处理 "## 依赖关系与阶段衔接" 之前的链接（避免依赖树打乱学习顺序）。
    - 忽略锚点链接 [[#...]] 和空目标 [[|...]]。
"""
import sys
import re
from pathlib import Path

WIKILINK_RE = re.compile(r"\[\[([^]|#\n\r]+)(?:#[^|\]]*)?(?:\|[^|\]]*)?\]\]")


def find_note_path(root: Path, title: str) -> Path | None:
    candidates = sorted(root.rglob(f"{title}.md"))
    if len(candidates) == 1:
        return candidates[0]
    if len(candidates) > 1:
        print(
            f"  警告：'{title}' 匹配到多个文件，使用第一个：{candidates[0]}",
            file=sys.stderr,
        )
        return candidates[0]
    return None


def parse_index_links(index_path: Path) -> list[str]:
    text = index_path.read_text(encoding="utf-8")
    # 依赖树通常放在最后，只用它来展示前置关系，不代表学习顺序
    cutoff = text.find("## 依赖关系与阶段衔接")
    if cutoff != -1:
        text = text[:cutoff]
    seen = set()
    result = []
    for m in WIKILINK_RE.finditer(text):
        target = m.group(1).strip()
        if not target:
            continue
        if target not in seen:
            seen.add(target)
            result.append(target)
    return result


def update_frontmatter(note_path: Path, order: int) -> bool:
    text = note_path.read_text(encoding="utf-8")
    if text.startswith("---"):
        end = text.find("---", 3)
        if end != -1:
            fm = text[3:end]
            rest = text[end + 3 :]
            # 已有 order（整数、字符串、浮点都兼容）
            if re.search(r"^order:\s*['\"]?\d+(\.0)?['\"]?\s*$", fm, re.MULTILINE):
                new_fm = re.sub(
                    r"^order:\s*['\"]?\d+(\.0)?['\"]?\s*$",
                    f"order: {order}",
                    fm,
                    flags=re.MULTILINE,
                    count=1,
                )
                if new_fm == fm:
                    return False
                note_path.write_text(
                    f"---\n{new_fm}---{rest}", encoding="utf-8"
                )
                return True
            # 没有 order，把它放到 frontmatter 最前面
            new_fm = f"order: {order}\n{fm.lstrip()}"
            note_path.write_text(
                f"---\n{new_fm}---{rest}", encoding="utf-8"
            )
            return True
    # 没有 frontmatter，新增一个
    note_path.write_text(
        f"---\norder: {order}\n---\n\n{text}", encoding="utf-8"
    )
    return True


def main():
    if len(sys.argv) < 2:
        print("用法: python scripts/apply-index-order.py <索引文件路径>", file=sys.stderr)
        sys.exit(1)

    index_path = Path(sys.argv[1]).resolve()
    if not index_path.exists():
        print(f"错误：索引文件不存在 {index_path}", file=sys.stderr)
        sys.exit(1)

    root = index_path.parent
    print(f"处理索引: {index_path.relative_to(Path.cwd())}")
    links = parse_index_links(index_path)
    print(f"发现 {len(links)} 个唯一外部链接（按首次出现顺序）")

    matched = 0
    updated = 0
    for order, title in enumerate(links, start=1):
        path = find_note_path(root, title)
        if path:
            matched += 1
            changed = update_frontmatter(path, order)
            if changed:
                updated += 1
                print(f"  [{order:>3}] {title}")
            else:
                print(f"  [{order:>3}] {title} (无需更新)")
        else:
            print(f"  [{order:>3}] {title} -> (未找到文件)")

    # 索引文件自身放在最前
    if update_frontmatter(index_path, 0):
        updated += 1
        print("  [  0] 索引文件自身")
    else:
        print("  [  0] 索引文件自身 (无需更新)")

    print(f"匹配 {matched}/{len(links)} 个笔记，共更新 {updated} 个文件")


if __name__ == "__main__":
    main()

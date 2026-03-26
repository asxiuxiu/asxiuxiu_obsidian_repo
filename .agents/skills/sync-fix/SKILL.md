---
name: sync-fix
description: 修复同步过程中的常见问题，包括分支非直线历史和 commit 信息不符合规范。
disable-model-invocation: true
---

# Sync Fix

修复同步过程中的常见问题，确保仓库状态健康。

## 功能

- 检测并修复非直线分支历史（通过 rebase 变基）
- 检测并修复不符合规范的 commit 信息
- 交互式确认破坏性操作

## 执行

```bash
CURRENT_BRANCH=$(git branch --show-current)
REMOTE="origin"

echo "🔧 开始检查并修复同步问题..."
echo ""

# ============ 检查 1: 分支是否为直线历史 ============
echo "📋 检查 1: 分支历史是否线性..."

# 获取当前分支与远程分支的共同祖先
MERGE_BASE=$(git merge-base $CURRENT_BRANCH $REMOTE/$CURRENT_BRANCH 2>/dev/null || true)

if [ -n "$MERGE_BASE" ]; then
    # 检查是否有 merge commit
    MERGE_COMMITS=$(git rev-list --merges $MERGE_BASE..$CURRENT_BRANCH 2>/dev/null | wc -l)

    if [ "$MERGE_COMMITS" -gt 0 ]; then
        echo "  ⚠️ 发现 $MERGE_COMMITS 个 merge commit，分支历史非直线"
        echo ""
        echo "  最近历史："
        git log --oneline --graph -5
        echo ""
        echo "  建议: 执行 rebase 重写为直线历史"
        echo "  命令: git rebase $REMOTE/$CURRENT_BRANCH"
        echo ""
        echo "  ⚠️ 这将重写 commit hash，仅适用于未推送的更改"
        echo "  是否继续？(yes/no)"
        exit 0
    else
        echo "  ✅ 分支历史为直线"
    fi
else
    echo "  ⚠️ 无法确定与远程分支的关系"
fi

echo ""

# ============ 检查 2: Commit 信息规范 ============
echo "📋 检查 2: Commit 信息是否符合规范..."

# 获取最近 10 个 commit
VIOLATIONS=""
COMMIT_COUNT=0

for commit in $(git rev-list -10 $CURRENT_BRANCH 2>/dev/null); do
    msg=$(git log -1 --pretty=format:"%s" $commit)
    COMMIT_COUNT=$((COMMIT_COUNT + 1))

    # 检查是否符合 vault backup 格式
    if echo "$msg" | grep -qE "^Vault backup: [0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}$"; then
        continue
    fi

    VIOLATIONS="$VIOLATIONS  $commit: $msg\n"
done

if [ -n "$VIOLATIONS" ]; then
    echo "  ⚠️ 发现以下 commit 信息不符合规范："
    echo ""
    printf "$VIOLATIONS"
    echo ""
    echo "  规范格式:"
    echo "    Vault backup: YYYY-MM-DD HH:MM:SS"
    echo ""
    echo "  提示: commit 信息仅作为时间戳标记，内容由文件本身体现"
else
    echo "  ✅ 最近 $COMMIT_COUNT 个 commit 信息符合规范"
fi

echo ""
echo "✅ 检查完成"
```

如果用户确认修复非直线历史，执行：

```bash
CURRENT_BRANCH=$(git branch --show-current)
REMOTE="origin"

echo "🔧 正在修复分支历史..."

# 暂存本地未提交更改
STASHED=false
if ! git diff --quiet || ! git diff --cached --quiet; then
    echo "📦 暂存本地更改..."
    git stash push -m "sync-fix-$(date +%s)"
    STASHED=true
fi

# 执行 rebase
if git rebase $REMOTE/$CURRENT_BRANCH; then
    echo "✅ 分支历史已修复为直线"
    echo ""
    git log --oneline --graph -5
else
    echo ""
    echo "❌ Rebase 失败，存在冲突"
    echo "解决后执行: git rebase --continue"
    echo "或放弃: git rebase --abort"
    exit 1
fi

# 恢复暂存
if [ "$STASHED" = true ]; then
    echo ""
    echo "📦 恢复本地更改..."
    git stash pop
fi

echo ""
echo "✅ 修复完成"
```

## 使用场景

| 问题 | 检测 | 修复方式 |
|------|------|---------|
| Merge commit 导致非直线 | ✅ | rebase 变基 |
| Commit 信息不规范 | ✅ | 提示规范 |
| 本地有未提交更改 | ✅ | 自动 stash/pop |

## 注意事项

- 修复历史会重写 commit hash，仅适用于未推送的更改
- 已推送到远程的 commit 修改后需强制推送
- 执行前会自动暂存本地未提交更改

---
name: sync-pull
description: 从远程仓库拉取最新更改，优先保持分支为直线历史。发生冲突时询问用户是否强制 reset 到远端版本。
disable-model-invocation: false
---

# Sync Pull

从远程仓库拉取最新更改，**优先保持分支为直线历史**。

## 功能

- 首先尝试使用 rebase 拉取，保持直线历史
- 如果发生冲突，询问是否强制 reset（丢弃本地更改）
- 用户确认后才执行破坏性操作

## 执行

```bash
CURRENT_BRANCH=$(git branch --show-current)
REMOTE="origin"

echo "正在尝试同步远程更新（使用 rebase 保持直线历史）..."

# 尝试使用 rebase 拉取，自动暂存本地更改
if git pull --rebase --autostash $REMOTE $CURRENT_BRANCH 2>&1; then
    echo ""
    echo "✅ 同步完成！分支保持直线历史"
    echo ""
    echo "最近3次提交："
    git log --oneline -3
    exit 0
fi

# 拉取失败，显示状态
echo ""
echo "⚠️ 同步失败，检测到冲突或无法自动合并"
echo ""
echo "当前状态："
git status --short
echo ""
echo "最近的提交历史："
git log --oneline --graph -5 --all
echo ""
echo "您可以："
echo "  1. 手动解决冲突后完成 rebase"
echo "  2. 或者放弃本地更改，强制 reset 到远端版本"
echo ""
echo "是否强制重置到远端版本？这将丢弃所有本地未提交的更改！(yes/no)"
```

如果用户确认重置，执行：

```bash
CURRENT_BRANCH=$(git branch --show-current)
REMOTE="origin"

# 中止可能正在进行的 rebase
git rebase --abort 2>/dev/null || true

# 强制重置到远端版本
echo "正在强制重置到远端版本: $REMOTE/$CURRENT_BRANCH"
git reset --hard $REMOTE/$CURRENT_BRANCH

echo ""
echo "✅ 已强制同步到远端版本"
echo ""
echo "最近3次提交："
git log --oneline -3
```

## 注意事项

- 优先使用 `git pull --rebase --autostash` 保持直线历史
- 冲突时由用户决定是否放弃本地更改
- 强制 reset 前会再次确认，避免误操作

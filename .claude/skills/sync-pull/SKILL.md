---
name: sync-pull
description: 从远程仓库拉取最新更改，同步到本地 Obsidian vault。处理本地未提交更改的保存和恢复。
disable-model-invocation: false
---

# Sync Pull

从远程仓库拉取最新更改，同步到本地 Obsidian vault。

## 功能

- 拉取远程最新更改
- 处理可能的冲突
- 显示同步状态

## 步骤

1. 检查仓库状态
2. 保存本地未提交的更改（如有）
3. 拉取远程更新
4. 恢复本地更改（如有）
5. 显示同步结果

## 执行

```bash
# 检查是否有未提交的更改
if ! git diff --quiet || ! git diff --cached --quiet; then
    echo "本地有未提交的更改，正在保存..."
    git stash push -m "Auto-stash before pull $(date +%Y-%m-%d_%H:%M:%S)"
    STASHED=true
else
    STASHED=false
fi

# 拉取远程更新
echo "正在拉取远程更新..."
git pull origin $(git branch --show-current)
PULL_RESULT=$?

# 恢复本地更改
if [ "$STASHED" = true ]; then
    echo "恢复本地未提交的更改..."
    git stash pop
fi

if [ $PULL_RESULT -eq 0 ]; then
    echo "✅ 同步完成！"
    echo ""
    echo "最近3次提交："
    git log --oneline -3
else
    echo "❌ 拉取失败，请检查错误信息"
    exit 1
fi
```

## 注意事项

- 如果本地和远程有冲突，需要手动解决
- 建议定期同步以避免大量冲突

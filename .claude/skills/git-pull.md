# Git Pull

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

## 工具

使用 Bash 工具执行 Git 命令。

## 执行

执行以下命令：

```bash
# 检查是否有未提交的更改
if ! git diff --quiet || ! git diff --cached --quiet; then
    echo "本地有未提交的更改，正在保存..."
    git stash push -m "Auto-stash before pull $(date +%Y-%m-%d_%H:%M:%S)"
    STASHED=true
fi

# 拉取远程更新
git pull origin $(git branch --show-current)

# 恢复本地更改
if [ "$STASHED" = true ]; then
    echo "恢复本地未提交的更改..."
    git stash pop
fi

echo "同步完成！"
git log --oneline -3
```

## 别名

- `git-sync`
- `git-update`
- `pull`
- `同步`

## 注意事项

- 如果本地和远程有冲突，需要手动解决
- 建议定期同步以避免大量冲突

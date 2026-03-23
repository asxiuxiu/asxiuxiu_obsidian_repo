---
name: sync-push
description: 提交本地更改并推送到远程仓库，备份 Obsidian vault。
disable-model-invocation: false
---

# Sync Push

提交本地更改并推送到远程仓库，备份 Obsidian vault。

## 功能

- 显示修改的文件
- 添加更改到暂存区
- 提交更改（带提交信息）
- 推送到远程仓库

## 参数

- `$ARGUMENTS`: 提交信息（可选，默认使用自动生成的时间戳信息）

## 执行

```bash
# 检查是否有更改
if git diff --quiet && git diff --cached --quiet; then
    echo "没有需要提交的更改"
    exit 0
fi

# 显示修改的文件
echo "以下文件将被提交："
git status --short

# 添加所有更改
git add .

# 提交更改（使用传入的参数或默认值）
if [ -n "$ARGUMENTS" ]; then
    COMMIT_MSG="$ARGUMENTS"
else
    COMMIT_MSG="Vault backup: $(date +'%Y-%m-%d %H:%M:%S')"
fi
git commit -m "$COMMIT_MSG"

# 推送到远程
git push origin $(git branch --show-current)

echo "✅ 成功推送到远程仓库！"
git log --oneline -1
```

## 注意事项

- 如果远程有更新，需要先执行 sync-pull 同步
- 建议定期备份（如每天或每次重要修改后）

## 提交信息规范

**此仓库是 Obsidian 笔记备份仓库，不需要在 commit 信息中描述具体内容。**

- 默认使用时间戳格式：`Vault backup: YYYY-MM-DD HH:MM:SS`
- 不要总结修改内容（如"新增xxx笔记"、"更新xxx"等）
- 如用户传入 `$ARGUMENTS`，仍使用传入的内容（允许例外情况）
- 原则：commit 只是定期备份的时间戳，内容由文件本身体现

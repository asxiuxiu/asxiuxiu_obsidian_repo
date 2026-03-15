# Git Push

提交本地更改并推送到远程仓库，备份 Obsidian vault。

## 功能

- 显示修改的文件
- 添加更改到暂存区
- 提交更改（带提交信息）
- 推送到远程仓库

## 参数

- `message`: 提交信息（可选，默认使用自动生成的时间戳信息）

## 步骤

1. 检查仓库状态
2. 显示修改的文件列表
3. 添加所有更改
4. 提交更改
5. 推送到远程仓库

## 工具

使用 Bash 工具执行 Git 命令。

## 执行

如果没有提供提交信息，使用默认格式：`Vault backup: YYYY-MM-DD HH:MM:SS`

执行以下命令：

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

# 提交更改
COMMIT_MSG="${message:-Vault backup: $(date +'%Y-%m-%d %H:%M:%S')}"
git commit -m "$COMMIT_MSG"

# 推送到远程
git push origin $(git branch --show-current)

echo "✅ 成功推送到远程仓库！"
git log --oneline -1
```

## 别名

- `git-commit`
- `git-backup`
- `push`
- `提交`
- `备份`

## 注意事项

- 如果远程有更新，需要先执行 git-pull 同步
- 提交信息可以使用中文
- 建议定期备份（如每天或每次重要修改后）

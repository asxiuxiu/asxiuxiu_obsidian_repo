# Git Init

初始化 Obsidian vault 的 Git 仓库，配置用户信息，并连接远程仓库。

## 信息

- **用户名**: asxiuxiu
- **邮箱**: licheng1996121@gmail.com
- **远程仓库**: https://github.com/asxiuxiu/asxiuxiu_obsidian_repo.git

## 步骤

1. 初始化 Git 仓库
2. 配置用户名和邮箱
3. 添加远程仓库
4. 创建初始提交
5. 推送到远程仓库

## 工具

使用 Bash 工具执行 Git 命令。

## 执行

执行以下命令：

```bash
git init
git config user.name "asxiuxiu"
git config user.email "licheng1996121@gmail.com"
git remote add origin https://github.com/asxiuxiu/asxiuxiu_obsidian_repo.git

# 创建 .gitignore 忽略 Obsidian 配置和缓存
echo ".obsidian/workspace.json
.obsidian/graph.json
.obsidian/plugins/*/data.json
.obsidian/cache
.DS_Store
*.tmp" > .gitignore

git add .
git commit -m "Initial commit: Obsidian vault setup"
git push -u origin main || git push -u origin master
```

## 注意事项

- 首次推送可能需要输入 GitHub 凭据（用户名 + Personal Access Token）
- 建议设置 SSH 密钥以避免每次输入密码
- 如果远程仓库已有内容，可能需要先 pull 再 push

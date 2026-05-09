---
name: git-sync
description: Git 同步管理 skill。当用户要求备份 vault、提交更改、推送到远程、拉取更新、修复同步问题或整理分支历史时触发。提供 push / pull / fix 三个子命令，统一 commit 规范，优先保持直线历史。
disable-model-invocation: false
---

# Git Sync

Git 同步管理，覆盖 vault 的"推-拉-修"完整工作流。

## Commit 规范（强制）

**唯一允许的格式**：`Vault backup: YYYY-MM-DD HH:mm:ss`

- 时间使用 24 小时制
- 不允许自定义 commit message（包括 `feat:` / `fix:` 等 Conventional Commit 前缀）
- 不允许总结修改内容（如"新增xxx笔记"）
- **原则**：内容承载在文件本身，commit 仅作为时间戳标记

如果用户尝试传入自定义提交信息，**拒绝并提示**上述规范。

---

## 子命令：push

**触发词**："备份 vault"、"提交更改"、"推送到远程"、"git push"

### 功能

- 显示修改的文件
- 添加所有更改到暂存区
- 提交（强制使用 `Vault backup: 时间戳` 格式）
- 推送到远程仓库

### 执行

```powershell
# 检查是否有更改
$hasChanges = (git status --short) -ne $null
if (-not $hasChanges) {
    Write-Host "没有需要提交的更改"
    exit 0
}

# 显示修改的文件
Write-Host "以下文件将被提交："
git status --short

# 添加所有更改
git add .

# 提交（强制时间戳格式）
$commitMsg = "Vault backup: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
git commit -m "$commitMsg"

# 推送到远程
$branch = git branch --show-current
git push origin $branch

Write-Host "成功推送到远程仓库！"
git log --oneline -1
```

---

## 子命令：pull

**触发词**："拉取远程更新"、"同步"、"pull"、"git pull"

### 功能

- 优先使用 `git pull --rebase --autostash` 保持直线历史
- 如果发生冲突，询问用户是否强制 reset（丢弃本地更改）
- 用户确认后才执行破坏性操作

### 执行

```powershell
$currentBranch = git branch --show-current
$remote = "origin"

Write-Host "正在尝试同步远程更新（使用 rebase 保持直线历史）..."

# 尝试使用 rebase 拉取，自动暂存本地更改
git pull --rebase --autostash $remote $currentBranch 2>&1
if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "同步完成！分支保持直线历史"
    Write-Host ""
    Write-Host "最近3次提交："
    git log --oneline -3
    exit 0
}

# 拉取失败，显示状态
Write-Host ""
Write-Host "同步失败，检测到冲突或无法自动合并"
Write-Host ""
Write-Host "当前状态："
git status --short
Write-Host ""
Write-Host "最近的提交历史："
git log --oneline --graph -5 --all
Write-Host ""
Write-Host "您可以："
Write-Host "  1. 手动解决冲突后完成 rebase"
Write-Host "  2. 或者放弃本地更改，强制 reset 到远端版本"
Write-Host ""
Write-Host "是否强制重置到远端版本？这将丢弃所有本地未提交的更改！(yes/no)"
```

如果用户确认重置：

```powershell
$currentBranch = git branch --show-current
$remote = "origin"

# 中止可能正在进行的 rebase
git rebase --abort 2>$null

# 强制重置到远端版本
Write-Host "正在强制重置到远端版本: $remote/$currentBranch"
git reset --hard "$remote/$currentBranch"

Write-Host ""
Write-Host "已强制同步到远端版本"
Write-Host ""
Write-Host "最近3次提交："
git log --oneline -3
```

---

## 子命令：fix

**触发词**："修复同步问题"、"整理分支历史"、"commit 信息不规范"、"分支非直线"

### 功能

- 检测分支是否为直线历史（有无 merge commit）
- 检测 commit message 是否符合 `Vault backup: 时间戳` 规范
- 提供 rebase 修复和 cherry-pick 重建脚本
- 交互式确认破坏性操作

### 检查流程

```powershell
$currentBranch = git branch --show-current
$remote = "origin"

Write-Host "开始检查同步问题..."
Write-Host ""

# ============ 检查 1: 分支是否为直线历史 ============
Write-Host "检查 1: 分支历史是否线性..."

$mergeBase = git merge-base $currentBranch "$remote/$currentBranch" 2>$null
if ($mergeBase) {
    $mergeCommits = (git rev-list --merges "$mergeBase..$currentBranch" 2>$null | Measure-Object).Count
    if ($mergeCommits -gt 0) {
        Write-Host "  发现 $mergeCommits 个 merge commit，分支历史非直线"
        git log --oneline --graph -5
    } else {
        Write-Host "  分支历史为直线"
    }
} else {
    Write-Host "  无法确定与远程分支的关系"
}

Write-Host ""

# ============ 检查 2: Commit 信息规范 ============
Write-Host "检查 2: Commit 信息是否符合规范..."

$violations = @()
$commits = git log --format="%H|%ai|%s" -10 2>$null
$commitCount = 0

foreach ($line in $commits) {
    $parts = $line -split "\|", 3
    $hash = $parts[0]
    $date = $parts[1]
    $msg = $parts[2]
    $commitCount++

    if ($msg -notmatch "^Vault backup: \d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}$") {
        $violations += "  $($hash.Substring(0,7))  $date  $msg"
    }
}

if ($violations.Count -gt 0) {
    Write-Host "  发现以下 commit 信息不符合规范："
    $violations | ForEach-Object { Write-Host $_ }
    Write-Host ""
    Write-Host "  规范格式: Vault backup: YYYY-MM-DD HH:MM:SS"
} else {
    Write-Host "  最近 $commitCount 个 commit 信息符合规范"
}

Write-Host ""
Write-Host "检查完成"
```

### 修复 1: 非直线分支历史

适用于未推送的本地更改：

```powershell
$currentBranch = git branch --show-current
$remote = "origin"

Write-Host "正在修复分支历史..."

# 暂存本地未提交更改
$stashed = $false
if ((git diff --quiet; $LASTEXITCODE -ne 0) -or (git diff --cached --quiet; $LASTEXITCODE -ne 0)) {
    Write-Host "暂存本地更改..."
    git stash push -m "git-sync-fix-$(Get-Date -Format 'yyyyMMddHHmmss')"
    $stashed = $true
}

# 执行 rebase
if (git rebase "$remote/$currentBranch"; $LASTEXITCODE -eq 0) {
    Write-Host "分支历史已修复为直线"
    git log --oneline --graph -5
} else {
    Write-Host ""
    Write-Host "Rebase 失败，存在冲突"
    Write-Host "解决后执行: git rebase --continue"
    Write-Host "或放弃: git rebase --abort"
    exit 1
}

# 恢复暂存
if ($stashed) {
    Write-Host ""
    Write-Host "恢复本地更改..."
    git stash pop
}

Write-Host ""
Write-Host "修复完成"
```

### 修复 2: Commit Message 不规范

**警告**：这将重写 commit hash，需要强制推送。仅适用于个人仓库。

使用提供的 PowerShell 脚本一键修复：

```powershell
# 仅检测（不执行修复）
.\.agents\skills\git-sync\fix-commit-msg.ps1 -DryRun

# 交互式修复（推荐）
.\.agents\skills\git-sync\fix-commit-msg.ps1

# 检查最近 20 个 commit
.\.agents\skills\git-sync\fix-commit-msg.ps1 -CheckCount 20
```

脚本功能：
- 自动检测不规范 commit
- 自动创建备份分支
- 交互式确认（输入 yes 才执行）
- 失败自动回滚
- 保留备份分支供确认

### 手动修复步骤

1. **确认需要修复的 commit**：
```powershell
git log --format="%H %ai %s" -10
```

2. **创建备份分支**（重要！）：
```powershell
git branch backup-before-fix
```

3. **找到最后一个正确的 commit**：
```powershell
# 找到最后一个格式正确的 commit
$baseCommit = "<正确commit的hash>"
```

4. **重建历史（Cherry-pick 方法）**：
```powershell
# 从 base commit 创建新分支
git checkout -b fix-commits $baseCommit

# 逐个 cherry-pick 并修正消息
# 对每个需要修复的 commit：
git cherry-pick <commit-hash> --no-commit
git commit -m "Vault backup: YYYY-MM-DD HH:mm:ss"

# 完成后切回主分支并应用
git checkout main
git reset --hard fix-commits

# 强制推送（危险操作）
git push --force-with-lease origin main

# 清理
git branch -D fix-commits backup-before-fix
```

---

## PowerShell 兼容性

用户环境为 Windows PowerShell 5.1，需要注意：

| 问题 | 解决方式 |
|------|---------|
| 不支持 `&&` / `\|\|` | 使用 `if ($?) { ... }` 或 `cmd /c` |
| 命令链 | 使用 `;` 分隔而非 `&&` |
| 引号转义 | 注意 `"` 和 `'` 的使用 |

---

## 使用场景速查

| 问题 | 检测 | 修复方式 | 风险 |
|------|------|---------|------|
| 需要备份 vault | push | git add → commit → push | 无 |
| 需要拉取远程更新 | pull | git pull --rebase --autostash | 低（冲突时询问） |
| Merge commit 导致非直线 | fix | rebase 变基 | 低（未推送时） |
| Commit 信息不规范 | fix | cherry-pick 重建 | 高（需强制推送） |
| 本地有未提交更改 | pull/fix | 自动 stash/pop | 无 |

#!/usr/bin/env pwsh
# Sync Fix - Commit Message 修复脚本
# 自动检测并修复不符合规范的 commit message

param(
    [switch]$DryRun,      # 仅检测，不执行修复
    [switch]$Force,       # 跳过确认直接执行（危险！）
    [int]$CheckCount = 10 # 检查最近 N 个 commit
)

$ErrorActionPreference = "Stop"

# 颜色输出函数
function Write-Color($Text, $Color = "White") {
    Write-Host $Text -ForegroundColor $Color
}

# 检查 commit 是否符合规范
function Test-CommitMessage($Message) {
    return $Message -match "^Vault backup: \d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}$"
}

# 获取不规范的 commit 列表
function Get-InvalidCommits($Count) {
    $commits = @()
    $logOutput = git log --format="%H|%ai|%s" -$Count 2>$null
    
    foreach ($line in $logOutput) {
        $parts = $line -split "\|", 3
        $hash = $parts[0]
        $date = $parts[1]
        $msg = $parts[2]
        
        if (-not (Test-CommitMessage $msg)) {
            $commits += @{
                Hash = $hash
                Date = $date
                Message = $msg
                ShortHash = $hash.Substring(0, 7)
            }
        }
    }
    return $commits
}

# 主流程
Write-Color "🔧 Sync Fix - Commit Message 修复工具" "Cyan"
Write-Color "======================================" "Gray"
Write-Host ""

# 检查是否在 git 仓库
if (-not (git rev-parse --git-dir 2>$null)) {
    Write-Color "❌ 当前目录不是 git 仓库" "Red"
    exit 1
}

$currentBranch = git branch --show-current
Write-Color "当前分支: $currentBranch" "Yellow"
Write-Host ""

# 检测不规范 commit
Write-Color "📋 检测最近 $CheckCount 个 commit..." "Cyan"
$invalidCommits = Get-InvalidCommits $CheckCount

if ($invalidCommits.Count -eq 0) {
    Write-Color "✅ 所有 commit 消息符合规范！" "Green"
    exit 0
}

Write-Color "⚠️ 发现 $($invalidCommits.Count) 个不规范 commit:" "Yellow"
Write-Host ""

foreach ($c in $invalidCommits) {
    Write-Color "  $($c.ShortHash)  $($c.Date)  $($c.Message)" "Red"
}

Write-Host ""

if ($DryRun) {
    Write-Color "📝 仅检测模式，不执行修复" "Yellow"
    exit 0
}

# 确认修复
if (-not $Force) {
    Write-Color "⚠️ 警告：修复将重写 commit hash，需要强制推送！" "Red"
    Write-Host ""
    $confirm = Read-Host "是否继续修复？(yes/no)"
    if ($confirm -ne "yes") {
        Write-Color "❌ 已取消" "Yellow"
        exit 0
    }
}

# 开始修复
Write-Host ""
Write-Color "🔧 开始修复..." "Cyan"

# 1. 创建备份
$backupBranch = "backup-fix-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
Write-Color "  1. 创建备份分支: $backupBranch" "Yellow"
git branch $backupBranch

# 2. 找到最后一个正确的 commit
$allCommits = git log --format="%H|%ai|%s" -$CheckCount
$baseCommit = $null

foreach ($line in $allCommits) {
    $parts = $line -split "\|", 3
    if (Test-CommitMessage $parts[2]) {
        $baseCommit = $parts[0]
    } else {
        break
    }
}

if (-not $baseCommit) {
    Write-Color "❌ 未找到规范的 base commit，请手动检查" "Red"
    exit 1
}

Write-Color "  2. Base commit: $($baseCommit.Substring(0, 7))" "Yellow"

# 3. 创建临时分支
$tempBranch = "temp-fix-$(Get-Random)"
Write-Color "  3. 创建修复分支..." "Yellow"
git checkout -b $tempBranch $baseCommit 2>$null

if ($LASTEXITCODE -ne 0) {
    Write-Color "❌ 创建分支失败" "Red"
    git checkout $currentBranch
    exit 1
}

# 4. 逐个 cherry-pick 并修复
try {
    $invalidCommits = @($invalidCommits)  # 确保是数组
    [array]::Reverse($invalidCommits)     # 反转顺序（从旧到新）
    
    foreach ($c in $invalidCommits) {
        $dateStr = [DateTime]::Parse($c.Date).ToString("yyyy-MM-dd HH:mm:ss")
        $newMsg = "Vault backup: $dateStr"
        
        Write-Color "  4. 修复 $($c.ShortHash) -> $newMsg" "Yellow"
        
        git cherry-pick $c.Hash --no-commit 2>$null
        if ($LASTEXITCODE -ne 0) {
            throw "Cherry-pick 失败: $($c.ShortHash)"
        }
        
        git commit -m $newMsg 2>$null
        if ($LASTEXITCODE -ne 0) {
            throw "Commit 失败: $($c.ShortHash)"
        }
    }
    
    # 5. 应用修复
    Write-Color "  5. 应用修复到 $currentBranch..." "Yellow"
    git checkout $currentBranch 2>$null
    git reset --hard $tempBranch 2>$null
    
    # 6. 推送
    Write-Color "  6. 强制推送到远程..." "Yellow"
    git push --force-with-lease origin $currentBranch 2>$null
    
    Write-Host ""
    Write-Color "✅ 修复完成！" "Green"
    Write-Host ""
    Write-Color "最近的 commit:" "Gray"
    git log --oneline -5
    
} catch {
    Write-Color "❌ 修复失败: $_" "Red"
    Write-Color "正在恢复..." "Yellow"
    
    git checkout $currentBranch 2>$null
    git reset --hard $backupBranch 2>$null
    
    Write-Color "已恢复到备份状态" "Yellow"
    exit 1
} finally {
    # 清理
    git branch -D $tempBranch 2>$null
}

Write-Host ""
Write-Color "💡 备份分支已保留: $backupBranch" "Gray"
Write-Color "   确认无误后可删除: git branch -D $backupBranch" "Gray"

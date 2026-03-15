#!/usr/bin/env bash
set -euo pipefail

# Sync Fix - Commit Message 修复脚本
# 自动检测并修复不符合规范的 commit message

# 默认值
DRY_RUN=false
FORCE=false
CHECK_COUNT=10

# 解析参数
while [[ $# -gt 0 ]]; do
    case $1 in
        --dry-run) DRY_RUN=true; shift ;;
        --force) FORCE=true; shift ;;
        --check-count) CHECK_COUNT="$2"; shift 2 ;;
        *) echo "未知参数: $1"; exit 1 ;;
    esac
done

# 颜色输出
color_echo() {
    local color="$1"
    local text="$2"
    case "$color" in
        red)    echo -e "\033[31m$text\033[0m" ;;
        green)  echo -e "\033[32m$text\033[0m" ;;
        yellow) echo -e "\033[33m$text\033[0m" ;;
        cyan)   echo -e "\033[36m$text\033[0m" ;;
        gray)   echo -e "\033[90m$text\033[0m" ;;
        *)      echo "$text" ;;
    esac
}

# 检查 commit 是否符合规范
test_commit_message() {
    local msg="$1"
    [[ "$msg" =~ ^Vault\ backup:\ [0-9]{4}-[0-9]{2}-[0-9]{2}\ [0-9]{2}:[0-9]{2}:[0-9]{2}$ ]]
}

# 获取不规范的 commit 列表
get_invalid_commits() {
    local count="$1"
    git log --format="%H|%ai|%s" -"$count" 2>/dev/null | while IFS='|' read -r hash date msg; do
        if ! test_commit_message "$msg"; then
            echo "$hash|$date|$msg"
        fi
    done
}

echo ""
color_echo "cyan" "🔧 Sync Fix - Commit Message 修复工具"
color_echo "gray" "======================================"
echo ""

# 检查是否在 git 仓库
if ! git rev-parse --git-dir >/dev/null 2>&1; then
    color_echo "red" "❌ 当前目录不是 git 仓库"
    exit 1
fi

currentBranch=$(git branch --show-current)
color_echo "yellow" "当前分支: $currentBranch"
echo ""

# 检测不规范 commit
color_echo "cyan" "📋 检测最近 $CHECK_COUNT 个 commit..."
mapfile -t invalidCommits < <(get_invalid_commits "$CHECK_COUNT")

if [ ${#invalidCommits[@]} -eq 0 ] || [ -z "${invalidCommits[0]}" ]; then
    color_echo "green" "✅ 所有 commit 消息符合规范！"
    exit 0
fi

color_echo "yellow" "⚠️ 发现 ${#invalidCommits[@]} 个不规范 commit:"
echo ""

for line in "${invalidCommits[@]}"; do
    IFS='|' read -r hash date msg <<< "$line"
    shortHash="${hash:0:7}"
    color_echo "red" "  $shortHash  $date  $msg"
done

echo ""

if [ "$DRY_RUN" = true ]; then
    color_echo "yellow" "📝 仅检测模式，不执行修复"
    exit 0
fi

# 确认修复
if [ "$FORCE" = false ]; then
    color_echo "red" "⚠️ 警告：修复将重写 commit hash，需要强制推送！"
    echo ""
    read -rp "是否继续修复？(yes/no) " confirm
    if [ "$confirm" != "yes" ]; then
        color_echo "yellow" "❌ 已取消"
        exit 0
    fi
fi

# 开始修复
echo ""
color_echo "cyan" "🔧 开始修复..."

# 1. 创建备份
backupBranch="backup-fix-$(date '+%Y%m%d-%H%M%S')"
color_echo "yellow" "  1. 创建备份分支: $backupBranch"
git branch "$backupBranch"

# 2. 找到最后一个正确的 commit
baseCommit=""
while IFS='|' read -r hash date msg; do
    if test_commit_message "$msg"; then
        baseCommit="$hash"
    else
        break
    fi
done < <(git log --format="%H|%ai|%s" -"$CHECK_COUNT")

if [ -z "$baseCommit" ]; then
    color_echo "red" "❌ 未找到规范的 base commit，请手动检查"
    exit 1
fi

shortBase="${baseCommit:0:7}"
color_echo "yellow" "  2. Base commit: $shortBase"

# 3. 创建临时分支
tempBranch="temp-fix-$RANDOM"
color_echo "yellow" "  3. 创建修复分支..."
if ! git checkout -b "$tempBranch" "$baseCommit" >/dev/null 2>&1; then
    color_echo "red" "❌ 创建分支失败"
    git checkout "$currentBranch"
    exit 1
fi

# 恢复函数
restore() {
    color_echo "yellow" "正在恢复..."
    git checkout "$currentBranch" >/dev/null 2>&1 || true
    git reset --hard "$backupBranch" >/dev/null 2>&1 || true
    color_echo "yellow" "已恢复到备份状态"
}

# 4. 逐个 cherry-pick 并修复（反转数组：从旧到新）
declare -a orderedCommits=()
for ((i=${#invalidCommits[@]}-1; i>=0; i--)); do
    orderedCommits+=("${invalidCommits[i]}")
done

for line in "${orderedCommits[@]}"; do
    IFS='|' read -r hash date msg <<< "$line"
    shortHash="${hash:0:7}"
    # 解析日期并格式化
    dateStr=$(date -d "$date" '+%Y-%m-%d %H:%M:%S' 2>/dev/null || echo "$date")
    newMsg="Vault backup: $dateStr"

    color_echo "yellow" "  4. 修复 $shortHash -> $newMsg"

    if ! git cherry-pick "$hash" --no-commit >/dev/null 2>&1; then
        color_echo "red" "❌ 修复失败: Cherry-pick 失败 $shortHash"
        restore
        git branch -D "$tempBranch" >/dev/null 2>&1 || true
        exit 1
    fi

    if ! git commit -m "$newMsg" >/dev/null 2>&1; then
        color_echo "red" "❌ 修复失败: Commit 失败 $shortHash"
        restore
        git branch -D "$tempBranch" >/dev/null 2>&1 || true
        exit 1
    fi
done

# 5. 应用修复
color_echo "yellow" "  5. 应用修复到 $currentBranch..."
git checkout "$currentBranch" >/dev/null 2>&1
git reset --hard "$tempBranch" >/dev/null 2>&1

# 6. 推送
color_echo "yellow" "  6. 强制推送到远程..."
git push --force-with-lease origin "$currentBranch" >/dev/null 2>&1

echo ""
color_echo "green" "✅ 修复完成！"
echo ""
color_echo "gray" "最近的 commit:"
git log --oneline -5

# 清理
git branch -D "$tempBranch" >/dev/null 2>&1 || true

echo ""
color_echo "gray" "💡 备份分支已保留: $backupBranch"
color_echo "gray" "   确认无误后可删除: git branch -D $backupBranch"

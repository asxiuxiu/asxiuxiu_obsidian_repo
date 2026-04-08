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
- 自动处理已推送的 commit 历史重写

## Commit 规范

**强制格式**: `Vault backup: YYYY-MM-DD HH:MM:SS`

- 不允许自定义 commit message
- 时间戳使用原始 commit 的 author date
- 仅作为备份标记，内容由文件本身体现

## 执行检查

```bash
CURRENT_BRANCH=$(git branch --show-current)
REMOTE="origin"

echo "🔧 开始检查同步问题..."
echo ""

# ============ 检查 1: 分支是否为直线历史 ============
echo "📋 检查 1: 分支历史是否线性..."

MERGE_BASE=$(git merge-base $CURRENT_BRANCH $REMOTE/$CURRENT_BRANCH 2>/dev/null || true)

if [ -n "$MERGE_BASE" ]; then
    MERGE_COMMITS=$(git rev-list --merges $MERGE_BASE..$CURRENT_BRANCH 2>/dev/null | wc -l)

    if [ "$MERGE_COMMITS" -gt 0 ]; then
        echo "  ⚠️ 发现 $MERGE_COMMITS 个 merge commit，分支历史非直线"
        git log --oneline --graph -5
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

VIOLATIONS=""
COMMIT_COUNT=0

for commit in $(git rev-list -10 $CURRENT_BRANCH 2>/dev/null); do
    msg=$(git log -1 --pretty=format:"%s" $commit)
    COMMIT_COUNT=$((COMMIT_COUNT + 1))

    # 严格匹配格式
    if echo "$msg" | grep -qE "^Vault backup: [0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}$"; then
        continue
    fi

    VIOLATIONS="$VIOLATIONS  $commit: $msg\n"
done

if [ -n "$VIOLATIONS" ]; then
    echo "  ⚠️ 发现以下 commit 信息不符合规范："
    printf "$VIOLATIONS"
    echo ""
    echo "  规范格式: Vault backup: YYYY-MM-DD HH:MM:SS"
else
    echo "  ✅ 最近 $COMMIT_COUNT 个 commit 信息符合规范"
fi

echo ""
echo "✅ 检查完成"
```

## 修复操作

### 修复 1: 非直线分支历史

适用于未推送的本地更改：

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

### 修复 2: Commit Message 不规范

**⚠️ 警告**：这将重写 commit hash，需要强制推送。仅适用于个人仓库或有协调的团队。

#### 快速修复（推荐）

使用提供的 PowerShell 脚本一键修复：

```powershell
# 仅检测（不执行修复）
.\.agents\skills\sync-fix\fix-commit-msg.ps1 -DryRun

# 交互式修复（推荐）
.\.agents\skills\sync-fix\fix-commit-msg.ps1

# 检查最近 20 个 commit
.\.agents\skills\sync-fix\fix-commit-msg.ps1 -CheckCount 20
```

脚本功能：
- ✅ 自动检测不规范 commit
- ✅ 自动创建备份分支
- ✅ 交互式确认（输入 yes 才执行）
- ✅ 失败自动回滚
- ✅ 保留备份分支供确认

#### 手动修复步骤

1. **确认需要修复的 commit**：
```bash
git log --format="%H %ai %s" -10
```

2. **创建备份分支**（重要！）：
```bash
git branch backup-before-fix
```

3. **确定最后一个正确的 commit**：
```bash
# 找到最后一个格式正确的 commit
BASE_COMMIT="<正确commit的hash>"
```

4. **重建历史（Cherry-pick 方法）**：

```bash
# 从 base commit 创建新分支
git checkout -b fix-commits $BASE_COMMIT

# 逐个 cherry-pick 并修正消息
# 对每个需要修复的 commit：
git cherry-pick <commit-hash> --no-commit
git commit -m "Vault backup: YYYY-MM-DD HH:MM:SS"

# 完成后切回主分支并应用
git checkout main
git reset --hard fix-commits

# 强制推送（⚠️ 危险操作）
git push --force-with-lease origin main

# 清理
git branch -D fix-commits backup-before-fix
```

#### 批量修复示例

假设有以下不规范 commit：
- `abc1234`: `feat: add feature` (2026-03-31 10:00:00)
- `def5678`: `fix: bug fix` (2026-03-31 11:00:00)
- `ghi9999`: `Vault backup:` (缺少时间)

修复命令：

```bash
# 1. 备份
git branch backup-fix

# 2. 找到 base（最后一个正确的 commit）
BASE="319b1ba"

# 3. 重建分支
git checkout -b temp-fix $BASE

# 4. 逐个修复
git cherry-pick abc1234 --no-commit
git commit -m "Vault backup: 2026-03-31 10:00:00"

git cherry-pick def5678 --no-commit  
git commit -m "Vault backup: 2026-03-31 11:00:00"

git cherry-pick ghi9999 --no-commit
git commit -m "Vault backup: 2026-03-31 12:00:00"

# 5. 应用并推送
git checkout main
git reset --hard temp-fix
git push --force-with-lease origin main

# 6. 清理
git branch -D temp-fix backup-fix
```

## PowerShell 兼容性

用户环境为 Windows PowerShell 5.1，需要注意：

| 问题 | 解决方式 |
|------|---------|
| 不支持 `&&` / `\|\|` | 使用 `if ($?) { ... }` 或 `cmd /c` |
| 命令链 | 使用 `;` 分隔而非 `&&` |
| 引号转义 | 注意 `"` 和 `'` 的使用 |

示例：
```powershell
# ❌ 错误
command1 && command2

# ✅ 正确
cmd /c "command1 && command2"
# 或
command1
if ($?) { command2 }
```

## 快速修复流程图

```
发现问题
    │
    ▼
┌─────────────────┐
│ 执行检测脚本     │
│ (带 -DryRun)    │
└─────────────────┘
    │
    ├── 有违规? ──► 查看列表
    │                  │
    │                  ▼
    │           ┌─────────────────┐
    │           │ 执行修复脚本     │
    │           │ (不带参数)      │
    │           └─────────────────┘
    │                  │
    │                  ▼
    │           ┌─────────────────┐
    │           │ 输入 yes 确认   │
    │           └─────────────────┘
    │                  │
    │                  ▼
    │           ┌─────────────────┐
    │           │ 自动修复并推送   │
    │           │ (保留备份分支)  │
    │           └─────────────────┘
    │
    └── 无违规 ───► ✅ 完成
```

## 使用场景

| 问题 | 检测 | 修复方式 | 风险 |
|------|------|---------|------|
| Merge commit 导致非直线 | ✅ | rebase 变基 | 低（未推送时） |
| Commit 信息不规范 | ✅ | cherry-pick 重建 | 高（需强制推送） |
| 本地有未提交更改 | ✅ | 自动 stash/pop | 无 |

## 注意事项

1. **修复前必须创建备份分支**
2. **已推送的 commit 修改后需强制推送**（`--force-with-lease`）
3. **多人协作仓库慎用** - 会改变共享历史
4. **保持时间戳** - 使用原 commit 的 author date 作为新消息时间
5. **禁止自定义消息** - Vault backup 格式是唯一允许的格式

# 创建 Claude Code 所需的 junction 链接
# 运行方式: .\setup-claude-links.ps1

$ErrorActionPreference = "Stop"

function New-ClaudeJunction($from, $to) {
    $fromPath = Join-Path $PSScriptRoot $from
    $toPath = Join-Path $PSScriptRoot $to
    $fromDir = Split-Path $fromPath -Parent

    # 确保父目录存在
    if (-not (Test-Path $fromDir)) {
        New-Item -ItemType Directory -Path $fromDir | Out-Null
        Write-Host "[OK] 创建目录: $fromDir" -ForegroundColor Green
    }

    if (-not (Test-Path $toPath)) {
        Write-Host "[SKIP] 目标目录不存在: $toPath" -ForegroundColor Yellow
        return
    }

    # 如果 from 已存在但不是 junction，先删除
    if (Test-Path $fromPath) {
        $item = Get-Item $fromPath -Force
        if ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
            Write-Host "[OK] 已存在 junction: $from" -ForegroundColor Green
            return
        }
        Write-Host "[WARN] 删除现有目录: $from" -ForegroundColor Yellow
        Remove-Item -Recurse -Force $fromPath
    }

    # 创建 junction
    cmd /c mklink /J "$fromPath" "$toPath" | Out-Null
    Write-Host "[OK] 创建 junction: $from => $to" -ForegroundColor Green
}

New-ClaudeJunction ".claude\skills" ".agents\skills"

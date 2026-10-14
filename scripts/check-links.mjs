#!/usr/bin/env node
/**
 * 构建后：扫描 public/ 下所有 HTML，校验站内链接全部可解析，发现死链则退出码非零。
 * 作为 CI 红线：死链会让 deploy 失败，不会悄悄上线。
 *
 * 用法：node scripts/check-links.mjs [public目录]
 */
import fs from "node:fs"
import path from "node:path"

const publicDir = process.argv[2] || "public"

function walk(dir) {
  const out = []
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name)
    if (entry.isDirectory()) out.push(...walk(full))
    else out.push(full)
  }
  return out
}

const ATTR = /(?:href|src)="([^"]*)"/g
const SKIP_PREFIX = ["http://", "https://", "mailto:", "tel:", "data:", "javascript:"]

function resolveTarget(pageFile, raw) {
  let url = raw.trim()
  if (!url || url === "#") return null
  if (url.startsWith("#")) return null
  if (SKIP_PREFIX.some((p) => url.startsWith(p)) || url.startsWith("//")) return null
  url = url.split("#")[0].split("?")[0]
  if (!url) return null

  let rel
  if (url.startsWith("/")) {
    // 去掉 GitHub Pages 的项目前缀（站点部署在 /<repo>/ 下）
    rel = url.replace(/^\/[^/]+\//, "/").replace(/^\//, "")
    // 站点根路径（/ 或 /<repo>）指向首页
    if (rel === "" || rel === url.replace(/^\//, "").replace(/\/$/, "")) return null
  } else {
    rel = path
      .relative(publicDir, path.resolve(path.dirname(pageFile), decodeURI(url)))
      .split(path.sep)
      .join("/")
  }
  return decodeURI(rel)
}

function exists(rel) {
  const full = path.join(publicDir, rel)
  if (fs.existsSync(full)) return true
  // 目录形式的页面：notes/xxx -> notes/xxx/index.html
  if (fs.existsSync(path.join(full, "index.html"))) return true
  // 无扩展名的页面：notes/xxx -> notes/xxx.html
  if (fs.existsSync(full + ".html")) return true
  return false
}

const broken = []
let checked = 0

for (const file of walk(publicDir)) {
  if (!file.endsWith(".html")) continue
  const html = fs.readFileSync(file, "utf-8")
  for (const match of html.matchAll(ATTR)) {
    const rel = resolveTarget(file, match[1])
    if (!rel || rel.startsWith("..")) continue
    checked++
    if (!exists(rel)) {
      broken.push(`${path.relative(publicDir, file)} -> ${match[1]}`)
    }
  }
}

console.log(`[check-links] 检查 ${checked} 个站内链接`)
if (broken.length > 0) {
  const unique = [...new Set(broken)]
  console.error(`[check-links] 发现 ${unique.length} 处死链：`)
  for (const b of unique.slice(0, 50)) console.error(`  ✗ ${b}`)
  if (unique.length > 50) console.error(`  ... 还有 ${unique.length - 50} 处`)
  process.exit(1)
}
console.log("[check-links] 全部通过")

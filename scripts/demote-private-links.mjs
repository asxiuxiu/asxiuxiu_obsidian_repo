#!/usr/bin/env node
/**
 * 构建前：修复 / 降级指向「未发布或不存在笔记」的 wikilink。
 *
 * 背景：Bevy/UE/Agent 等目录在 workflow 中被 rsync 排除，不会出现在 content/ 里；
 * 且 Obsidian 按 basename 容错解析链接，笔记移动后旧路径在 Obsidian 里仍可跳转，
 * 但 Quartz 按完整路径严格解析，发布出来就是死链。
 *
 * 处理规则（对每个非 embed 的 wikilink）：
 *   1. 完整路径命中已发布集合 → 不动
 *   2. 含路径但未命中，basename 能唯一/最优匹配到已发布笔记 → 重写为正确路径（治愈）
 *   3. 裸 basename 能匹配 → 不动（Quartz 自己会做 basename 解析）
 *   4. 其它 → 降级为 <span class="unpublished-link"> 灰色纯文本（防死链）
 *
 * 用法：node scripts/demote-private-links.mjs [content目录]
 */
import fs from "node:fs"
import path from "node:path"
import { slugifyFilePath } from "@quartz-community/utils"

const contentDir = process.argv[2] || "content"

function walk(dir) {
  const out = []
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name)
    if (entry.isDirectory()) out.push(...walk(full))
    else out.push(full)
  }
  return out
}

const isMarkdown = (p) => /\.md$/i.test(p)

// 已发布笔记索引：完整路径集合（无扩展名）+ basename → 路径列表
const publishedPaths = new Set()
const byBasename = new Map()
for (const file of walk(contentDir)) {
  if (!isMarkdown(file)) continue
  const rel = path.relative(contentDir, file).split(path.sep).join("/").replace(/\.md$/i, "")
  publishedPaths.add(rel)
  publishedPaths.add(rel.toLowerCase())
  const base = path.basename(rel)
  if (!byBasename.has(base)) byBasename.set(base, [])
  byBasename.get(base).push(rel)
}

const IMAGE_EXT = /\.(png|jpe?g|gif|webp|svg|avif|bmp|excalidraw)$/i
const WIKILINK = /(!?)\[\[([^\[\]|#\n]+)?(#[^\[\]|\n]*)?(\|([^\[\]\n]*))?\]\]/g

// 表格行（含 callout 里的 `> | ...`）中的 wikilink：
// Quartz v5 的表格解析不吃 Obsidian 的 `\|` 转义，且表格单元格里的 wikilink 不会被处理，
// 必须在构建前直接转成 HTML <a>（Quartz 会把根相对 href 重写为当前页相对路径）。
const TABLE_ROW = /^\s*>?\s*\|/
const TABLE_WIKILINK = /\[\[([^\[\]\n]+?)\]\]/g

function slugifyAnchor(text) {
  return text.trim().toLowerCase().replace(/\s+/g, "-")
}

function normalize(target) {
  return target.replace(/\\/g, "/").replace(/^\.\//, "").replace(/\.md$/i, "")
}

function displayText(target, anchor, alias) {
  if (alias) return alias.trim()
  const base = path.basename(target || "")
  return (base + (anchor || "")).trim() || "未发布笔记"
}

// 从候选路径中选出最优：优先与源文件同顶级目录，其次路径最短
function pickBest(candidates, sourceRel) {
  const sourceTop = sourceRel.split("/")[1] // "Notes/<分类>/..."
  const sameTop = candidates.filter((c) => c.split("/")[1] === sourceTop)
  const pool = sameTop.length > 0 ? sameTop : candidates
  return pool.sort((a, b) => a.length - b.length)[0]
}

let healCount = 0
let demoteCount = 0
const report = []

for (const file of walk(contentDir)) {
  if (!isMarkdown(file)) continue
  const sourceRel = path.relative(contentDir, file).split(path.sep).join("/")
  const original = fs.readFileSync(file, "utf-8")
  const lines = original.split("\n")
  let inFence = false
  let heals = 0
  let demotes = 0

  const out = lines.map((line) => {
    if (/^\s*```/.test(line)) {
      inFence = !inFence
      return line
    }
    if (inFence) return line

    // 表格行：wikilink 转成 HTML <a> / 降级 span
    if (TABLE_ROW.test(line)) {
      return line.replace(TABLE_WIKILINK, (match, inner) => {
        // Obsidian 表格里的 wikilink 用 `\|` 分隔别名（`\|` 本身就是分隔符）
        const pipeParts = inner.split(/\\\|/)
        const headRaw = pipeParts[0]
        const alias = pipeParts.length > 1 ? pipeParts.slice(1).join("|").trim() : null
        const hashIdx = headRaw.indexOf("#")
        const target = (hashIdx >= 0 ? headRaw.slice(0, hashIdx) : headRaw).trim()
        const anchor = hashIdx >= 0 ? headRaw.slice(hashIdx + 1) : ""
        if (!target || IMAGE_EXT.test(target)) return match

        const norm = normalize(target)
        let resolved = null
        if (publishedPaths.has(norm) || publishedPaths.has(norm.toLowerCase())) {
          resolved = norm
        } else {
          const candidates = byBasename.get(path.basename(norm)) || []
          if (norm.includes("/") && candidates.length > 0) {
            resolved = pickBest(candidates, sourceRel)
            heals++
          } else if (!norm.includes("/") && candidates.length > 0) {
            resolved = candidates[0]
          }
        }
        if (!resolved) {
          demotes++
          const text = alias || path.basename(norm)
          return `<span class="unpublished-link" title="该笔记未发布">${text}</span>`
        }
        const href = slugifyFilePath(resolved) + (anchor ? `#${slugifyAnchor(anchor)}` : "")
        const text = alias || path.basename(resolved)
        return `<a href="${href}" class="internal-link">${text}</a>`
      })
    }

    return line.replace(WIKILINK, (match, bang, target, anchor, _pipe, alias) => {
      if (bang === "!") return match // embed 不动
      if (!target) return match // 纯锚点 [[#xxx]]
      if (IMAGE_EXT.test(target)) return match

      const norm = normalize(target)
      const hasPath = norm.includes("/")

      if (publishedPaths.has(norm) || publishedPaths.has(norm.toLowerCase())) return match

      const candidates = byBasename.get(path.basename(norm)) || []
      if (!hasPath) {
        // 裸 basename：Quartz 自己解析得动就保留
        return candidates.length > 0 ? match : demote()
      }
      if (candidates.length > 0) {
        // 路径失效但笔记还在别处：重写为正确路径
        const best = pickBest(candidates, sourceRel)
        heals++
        return `[[${best}${anchor || ""}${alias ? `|${alias}` : ""}]]`
      }
      return demote()

      function demote() {
        demotes++
        const text = displayText(target, anchor, alias)
        return `<span class="unpublished-link" title="该笔记未发布">${text}</span>`
      }
    })
  })

  if (heals > 0 || demotes > 0) {
    fs.writeFileSync(file, out.join("\n"))
    healCount += heals
    demoteCount += demotes
    report.push(`${sourceRel}: 治愈 ${heals}，降级 ${demotes}`)
  }
}

console.log(`[demote-private-links] 治愈 ${healCount} 处失效路径，降级 ${demoteCount} 处私有链接`)
for (const line of report) console.log(`  - ${line}`)

#!/usr/bin/env node
/**
 * 在 Quartz build 之后，把 frontmatter 里的 `order` 补进 contentIndex.json。
 * 这样客户端 Explorer 才能按 `order` 排序。
 *
 * 用法：
 *   node scripts/patch-content-index-order.mjs [content目录] [contentIndex.json路径]
 * 默认：
 *   node scripts/patch-content-index-order.mjs content public/static/contentIndex.json
 */
import fs from "node:fs"
import path from "node:path"
import YAML from "yaml"

const contentDir = process.argv[2] || "content"
const indexPath = process.argv[3] || "public/static/contentIndex.json"

function parseFrontmatter(filePath) {
  const text = fs.readFileSync(filePath, "utf-8")
  if (!text.startsWith("---")) return null
  const end = text.indexOf("---", 3)
  if (end === -1) return null
  try {
    return YAML.parse(text.slice(3, end).trim()) || {}
  } catch {
    return null
  }
}

if (!fs.existsSync(indexPath)) {
  console.error(`[patch-content-index] 找不到 ${indexPath}`)
  process.exit(1)
}

const raw = fs.readFileSync(indexPath, "utf-8")
const index = JSON.parse(raw)

let updated = 0
for (const [slug, entry] of Object.entries(index)) {
  const filePath = path.join(contentDir, entry.filePath)
  if (!fs.existsSync(filePath)) continue
  const fm = parseFrontmatter(filePath)
  if (fm && fm.order != null) {
    entry.order = Number(fm.order)
    updated++
  }
}

fs.writeFileSync(indexPath, JSON.stringify(index))
console.log(`[patch-content-index] 为 ${updated} 条笔记补入了 order`)

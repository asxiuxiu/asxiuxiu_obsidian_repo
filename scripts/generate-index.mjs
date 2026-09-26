#!/usr/bin/env node
/**
 * 构建前：扫描 content/Notes/ 的真实结构，自动生成首页 content/index.md。
 *
 * 设计原则：首页入口只来自磁盘上真实存在的文件夹和笔记，链接 slug 用 Quartz
 * 自己的 slugifyFilePath 计算，因此新增/重命名分类后首页自动正确，不会 404。
 *
 * 用法：node scripts/generate-index.mjs [content目录]
 */
import fs from "node:fs"
import path from "node:path"
import { slugifyFilePath } from "@quartz-community/utils"

const contentDir = process.argv[2] || "content"
const notesDir = path.join(contentDir, "Notes")

// 分类描述与排序：靠前优先展示；未列出的分类按名称排在后面，描述用兜底文案
const CATEGORY_META = {
  "C++编程": { desc: "从内存模型到并发、模板与标准库原理", order: 1 },
  动画系统: { desc: "骨骼、蒙皮、状态机与动画图的数据流", order: 2 },
  操作系统: { desc: "CPU、内存、并发与系统调用", order: 3 },
  构建系统: { desc: "CMake、编译链接与工程化实践", order: 4 },
  数学基础: { desc: "线性代数、微积分与三角函数", order: 5 },
  深度学习入门: { desc: "从零实现神经网络的入门笔记", order: 6 },
  图形学导读: { desc: "图形学概念入口与学习路线", order: 7 },
}

// Notes 根下不作为分类入口的文件
const SKIP_ROOT_FILES = new Set(["README.md", "index.md"])

const isMarkdown = (p) => /\.md$/i.test(p)

function countNotes(dir) {
  let n = 0
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name)
    if (entry.isDirectory()) n += countNotes(full)
    else if (isMarkdown(entry.name) && entry.name !== "索引.md") n++
  }
  return n
}

function cardHref(relPathNoExt, isDir) {
  const slug = slugifyFilePath(relPathNoExt.split(path.sep).join("/"))
  return isDir ? `${slug}/` : slug
}

const cards = []

if (fs.existsSync(notesDir)) {
  for (const entry of fs.readdirSync(notesDir, { withFileTypes: true })) {
    const meta = CATEGORY_META[entry.name] || {}
    if (entry.isDirectory()) {
      const dir = path.join(notesDir, entry.name)
      const indexNote = path.join(dir, "索引.md")
      // 优先链接到分类索引笔记（内容更聚焦），没有索引则链接到文件夹列表页
      const rel = fs.existsSync(indexNote)
        ? path.join("Notes", entry.name, "索引")
        : path.join("Notes", entry.name)
      cards.push({
        title: entry.name,
        desc: meta.desc || `${entry.name} 相关笔记`,
        order: meta.order ?? 100,
        count: countNotes(dir),
        href: cardHref(rel, !fs.existsSync(indexNote)),
      })
    } else if (isMarkdown(entry.name) && !SKIP_ROOT_FILES.has(entry.name)) {
      cards.push({
        title: entry.name.replace(/\.md$/i, ""),
        desc: meta.desc || "独立笔记",
        order: meta.order ?? 100,
        count: 1,
        href: cardHref(path.join("Notes", entry.name.replace(/\.md$/i, "")), false),
      })
    }
  }
}

cards.sort((a, b) => a.order - b.order || a.title.localeCompare(b.title, "zh-CN"))

const cardHtml = cards
  .map(
    (c) => `  <a class="category-card" href="${c.href}">
    <span class="card-title">${c.title}</span>
    <span class="card-desc">${c.desc}</span>
    <span class="card-count">${c.count} 篇笔记</span>
  </a>`,
  )
  .join("\n")

const indexMd = `---
title: asxiuxiu 的知识库
---

<div class="hero">
  <p class="hero-eyebrow">DIGITAL GARDEN</p>
  <p class="hero-tagline">游戏引擎、图形学与底层系统的学习笔记。<br/>这里记录的不是结论，而是思考本身。</p>
</div>

<div class="category-grid">
${cardHtml}
</div>
`

fs.writeFileSync(path.join(contentDir, "index.md"), indexMd)
console.log(`[generate-index] 生成首页，共 ${cards.length} 个入口：`)
for (const c of cards) console.log(`  - ${c.title} -> ${c.href}`)

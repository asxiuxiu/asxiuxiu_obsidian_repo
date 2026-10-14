#!/usr/bin/env node
/**
 * 本地预览服务器：模拟 GitHub Pages 的 URL 解析规则。
 * python -m http.server 不支持无扩展名页面（/notes/xxx -> /notes/xxx.html），
 * 用它预览会误判大量 404，所以本地验证一律用这个脚本。
 *
 * 用法：node scripts/serve-local.mjs [端口]   （默认 8080）
 * 访问：http://localhost:8080/asxiuxiu_obsidian_repo/
 */
import http from "node:http"
import fs from "node:fs"
import path from "node:path"

const port = Number(process.argv[2]) || 8080
const publicDir = path.resolve("public")
// GitHub Pages 项目站点前缀：本地用 _serve/<repo> 软链模拟，或直接在这里剥掉
const basePrefix = "/asxiuxiu_obsidian_repo"

const MIME = {
  ".html": "text/html; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".js": "application/javascript; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".png": "image/png",
  ".jpg": "image/jpeg",
  ".jpeg": "image/jpeg",
  ".gif": "image/gif",
  ".webp": "image/webp",
  ".svg": "image/svg+xml",
  ".woff2": "font/woff2",
  ".woff": "font/woff",
  ".xml": "application/xml",
  ".txt": "text/plain; charset=utf-8",
  ".webmanifest": "application/manifest+json",
}

function resolveFile(urlPath) {
  let rel = decodeURIComponent(urlPath)
  if (rel.startsWith(basePrefix)) rel = rel.slice(basePrefix.length)
  rel = rel.replace(/^\/+/, "")
  const candidates = [rel, rel + ".html", path.join(rel, "index.html")]
  for (const c of candidates) {
    const full = path.join(publicDir, c)
    if (fs.existsSync(full) && fs.statSync(full).isFile()) return { file: full, status: 200 }
  }
  // 404 兜底用站点自己的 404 页
  const notFound = path.join(publicDir, "404.html")
  return fs.existsSync(notFound) ? { file: notFound, status: 404 } : null
}

http
  .createServer((req, res) => {
    const urlPath = (req.url || "/").split("?")[0].split("#")[0]
    const resolved = resolveFile(urlPath)
    if (!resolved) {
      res.writeHead(404).end("not found")
      return
    }
    res.writeHead(resolved.status, {
      "content-type": MIME[path.extname(resolved.file).toLowerCase()] || "application/octet-stream",
    })
    fs.createReadStream(resolved.file).pipe(res)
  })
  .listen(port, () => {
    console.log(`本地预览：http://localhost:${port}${basePrefix}/`)
  })

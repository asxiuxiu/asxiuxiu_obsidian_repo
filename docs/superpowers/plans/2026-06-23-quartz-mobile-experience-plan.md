# Quartz 移动端体验优化实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 优化 Quartz v5 站点在移动端的目录可达性与视觉层级，减少 404 感知，并增加首页分类卡片导航。

**Architecture:** 通过 `quartz.config.yaml` 调整 Explorer 默认状态与 sidebar 布局；通过 `quartz/styles/custom.scss` 覆盖移动端 Explorer、底部固定导航、首页卡片等样式；通过 `main` 分支 workflow 生成带分类卡片的 `index.md`；所有改动均不修改已安装的 `.quartz/plugins` 源码，避免升级冲突。

**Tech Stack:** Quartz v5, SCSS, Preact/JSX (Quartz 组件), GitHub Actions

---

## 文件结构

| 文件 | 分支 | 职责 |
|------|------|------|
| `quartz.config.yaml` | v5 | 调整 Explorer 默认展开、sidebar 组件顺序 |
| `quartz.ts` | v5 | 为 Explorer 设置中文标题 `目录`，保持排序逻辑 |
 | `quartz/styles/custom.scss` | v5 | 移动端汉堡按钮可见性、Explorer 抽屉优化、首页卡片、间距优化 |
| `.github/workflows/deploy-notes.yml` | main | 生成新的首页 `index.md`，包含分类卡片网格 |

---

## Task 1: 调整 Explorer 默认状态与 sidebar 布局

**Files:**
- Modify: `quartz.config.yaml`

- [ ] **Step 1: 修改 Explorer 配置**

找到 `source: github:quartz-community/explorer` 段落，在 `layout` 下方添加 `options`，使桌面端目录默认展开：

```yaml
  - source: github:quartz-community/explorer
    enabled: true
    options:
      title: 目录
      folderDefaultState: open
      folderClickBehavior: link
      useSavedState: true
    layout:
      position: left
      priority: 50
```

- [ ] **Step 2: 调整左侧 sidebar 组件顺序**

确保左侧 sidebar 顺序为：page-title → search → spacer → darkmode → reader-mode → explorer（优先级决定顺序，越小越靠上）。当前配置中 search 优先级 20、spacer 25、darkmode 30、reader-mode 35、explorer 50，已符合。仅需确认 explorer 的 `priority: 50`。

- [ ] **Step 3: 提交 v5 配置改动**

```bash
cd /d/asxiuxiu_obsidian_repo-v5
git add quartz.config.yaml
git commit -m "feat(quartz): default explorer open and set Chinese title"
```

---

## Task 2: 设置 Explorer 中文标题

**Files:**
- Modify: `quartz.ts`

- [ ] **Step 1: 修改 Explorer 调用，显式传入中文标题**

将 `quartz.ts` 中 `ExternalPlugin.Explorer({...})` 的调用改为：

```ts
ExternalPlugin.Explorer({
  title: "目录",
  sortFn: (a, b) => {
    const orderA = getEffectiveOrder(a)
    const orderB = getEffectiveOrder(b)
    if (orderA !== orderB) return orderA - orderB
    return a.displayName.localeCompare(b.displayName, "zh-CN")
  },
})
```

- [ ] **Step 2: 提交 v5 改动**

```bash
cd /d/asxiuxiu_obsidian_repo-v5
git add quartz.ts
git commit -m "feat(quartz): set explorer title to 目录"
```

---

## Task 3: 移动端样式与导航优化

**Files:**
- Modify: `quartz/styles/custom.scss`

- [ ] **Step 1: 覆盖移动端 Explorer 汉堡按钮可见性**

在 `custom.scss` 中添加：

```scss
@use "./variables.scss" as *;

// 移动端：目录入口始终可见，且更加明显
@media all and ($mobile) {
  .explorer .mobile-explorer.hide-until-loaded {
    display: flex !important;
    align-items: center;
    justify-content: center;
    width: 2.5rem;
    height: 2.5rem;
    padding: 0.5rem;
    border-radius: 0.5rem;
    background: var(--lightgray);
    color: var(--dark);
  }

  .explorer .mobile-explorer .lucide-menu {
    stroke: var(--dark);
    stroke-width: 2.5;
  }
}
```

- [ ] **Step 2: 优化移动端 Explorer 抽屉**

覆盖 `.explorer.collapsed` 和 `.explorer-content`，避免抽屉被压缩为 34px 宽且不可见：

```scss
@media all and ($mobile) {
  .explorer {
    order: -1;
    width: auto;
    flex: 0 0 auto !important;
  }

  .explorer.collapsed {
    flex: 0 0 auto !important;
  }

  .explorer:not(.collapsed) {
    flex: 0 0 auto !important;
  }

  .explorer .explorer-content {
    max-width: 85vw;
    width: 85vw;
    padding: 4rem 1.5rem 2rem 1.5rem;
    box-shadow: 4px 0 24px rgba(0, 0, 0, 0.12);
  }
}
```

- [ ] **Step 3: 增加首页分类卡片样式**

```scss
.category-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(220px, 1fr));
  gap: 1rem;
  margin: 1.5rem 0;
}

.category-card {
  display: block;
  padding: 1.25rem;
  border: 1px solid var(--lightgray);
  border-radius: 0.75rem;
  background: var(--light);
  color: var(--dark);
  text-decoration: none;
  transition: transform 0.15s ease, box-shadow 0.15s ease, border-color 0.15s ease;
}

.category-card:hover {
  transform: translateY(-2px);
  box-shadow: 0 8px 24px rgba(0, 0, 0, 0.08);
  border-color: var(--secondary);
}

.category-card h3 {
  margin: 0 0 0.5rem 0;
  font-size: 1.1rem;
  color: var(--secondary);
}

.category-card p {
  margin: 0;
  font-size: 0.9rem;
  color: var(--darkgray);
  line-height: 1.4;
}

@media all and ($mobile) {
  .category-grid {
    grid-template-columns: 1fr;
    gap: 0.75rem;
  }

  .category-card {
    padding: 1rem;
  }
}
```

- [ ] **Step 4: 优化移动端搜索与标题**

```scss
@media all and ($mobile) {
  .page > #quartz-body .sidebar.left {
    padding-top: 1rem;
    gap: 0.75rem;
  }

  .search-button {
    min-height: 2.5rem;
    border-radius: 0.5rem;
    background: var(--lightgray);
    padding: 0 0.75rem;
  }

  .page-header {
    padding-top: 0.5rem;
  }

  article > h1 {
    font-size: 1.6rem;
    margin-top: 1.5rem;
  }
}
```

- [ ] **Step 5: 提交 v5 样式改动**

```bash
cd /d/asxiuxiu_obsidian_repo-v5
git add quartz/styles/custom.scss
git commit -m "feat(quartz): mobile explorer visibility, drawer, and category cards"
```

---

## Task 4: 更新首页内容，生成分类卡片

**Files:**
- Modify: `.github/workflows/deploy-notes.yml`（位于 main 分支）

- [ ] **Step 1: 切换回 main 分支工作区**

项目根目录 `/d/asxiuxiu_obsidian_repo` 已是 main 分支。

- [ ] **Step 2: 修改 workflow 中生成 index.md 的步骤**

找到以下步骤：

```yaml
      - name: Sync Notes and Assets into Quartz content
        run: |
          rm -rf quartz/content
          mkdir -p quartz/content
          rsync -a --exclude='Agent' --exclude='Bevy' --exclude='UE' vault/Notes/ quartz/content/Notes/
          cp -r vault/Assets quartz/content/
          cat > quartz/content/index.md << 'EOF'
          ---
          title: 首页
          ---

          欢迎来到 asxiuxiu 的知识库。

          > [[Notes/索引|进入知识索引]]
          EOF
```

替换为：

```yaml
      - name: Sync Notes and Assets into Quartz content
        run: |
          rm -rf quartz/content
          mkdir -p quartz/content
          rsync -a --exclude='Agent' --exclude='Bevy' --exclude='UE' vault/Notes/ quartz/content/Notes/
          cp -r vault/Assets quartz/content/
          cat > quartz/content/index.md << 'EOF'
          ---
          title: 首页
          ---

          欢迎来到 asxiuxiu 的知识库。

          <div class="category-grid">
            <a class="category-card" href="Notes/C++编程/">
              <h3>C++ 编程</h3>
              <p>从内存模型到并发、模板与标准库原理</p>
            </a>
            <a class="category-card" href="Notes/计算机图形学/">
              <h3>计算机图形学</h3>
              <p>渲染管线、GPU、Shader 与引擎渲染架构</p>
            </a>
            <a class="category-card" href="Notes/操作系统/">
              <h3>操作系统</h3>
              <p>CPU、内存、并发与系统调用</p>
            </a>
            <a class="category-card" href="Notes/构建系统/">
              <h3>构建系统</h3>
              <p>CMake、编译链接与工程化实践</p>
            </a>
            <a class="category-card" href="Notes/数学基础/">
              <h3>数学基础</h3>
              <p>线性代数、微积分、三角函数与复分析</p>
            </a>
            <a class="category-card" href="Notes/人工智能/">
              <h3>人工智能</h3>
              <p>深度学习、具身智能与跨域整合</p>
            </a>
            <a class="category-card" href="Notes/游戏引擎杂谈碎片/">
              <h3>引擎杂谈</h3>
              <p>游戏引擎设计与源码分析碎片</p>
            </a>
            <a class="category-card" href="Notes/索引/">
              <h3>知识总索引</h3>
              <p>按学习顺序组织的完整索引</p>
            </a>
          </div>
          EOF
```

注意：href 使用 `Notes/C++编程/` 这种文件夹路径，Quartz 的 `crawl-links` 插件会解析为内部链接。由于路径中包含 `+`，无需额外编码。

- [ ] **Step 3: 提交 main 分支改动**

```bash
cd /d/asxiuxiu_obsidian_repo
git add .github/workflows/deploy-notes.yml
git commit -m "feat(deploy): add category cards to homepage"
```

---

## Task 5: 本地精简构建验证

**Files:**
- Test: 通过 `npx quartz build` 与浏览器/截图验证

- [ ] **Step 1: 在 v5 worktree 中准备精简内容**

```bash
cd /d/asxiuxiu_obsidian_repo-v5
rm -rf content public
mkdir -p content/Notes content/Assets
cp -r /d/asxiuxiu_obsidian_repo/Notes/索引 content/Notes/
cp -r /d/asxiuxiu_obsidian_repo/Notes/计算机图形学 content/Notes/
cp -r /d/asxiuxiu_obsidian_repo/Assets/* content/Assets/ 2>/dev/null || true
cat > content/index.md << 'EOF'
---
title: 首页
---

欢迎来到 asxiuxiu 的知识库。

<div class="category-grid">
  <a class="category-card" href="Notes/计算机图形学/">
    <h3>计算机图形学</h3>
    <p>渲染管线、GPU、Shader 与引擎渲染架构</p>
  </a>
  <a class="category-card" href="Notes/索引/">
    <h3>知识总索引</h3>
    <p>按学习顺序组织的完整索引</p>
  </a>
</div>
EOF
```

- [ ] **Step 2: 执行构建**

```bash
cd /d/asxiuxiu_obsidian_repo-v5
npx quartz build
```

Expected output contains:

```
Done processing N files in Xs
```

- [ ] **Step 3: 检查生成的 CSS 与 HTML**

```bash
cd /d/asxiuxiu_obsidian_repo-v5
grep -q "category-grid" public/index.html && echo "category cards rendered"
ls public/index.css public/static/contentIndex.json
```

- [ ] **Step 4: 验证移动端汉堡按钮可见性**

在生成的首页 HTML 中确认：

```bash
grep -o '<button[^>]*class="[^"]*mobile-explorer[^"]*"[^>]*>' public/index.html
```

应不再包含 `hide-until-loaded`。

- [ ] **Step 5: 启动本地服务并截图（可选但推荐）**

```bash
cd /d/asxiuxiu_obsidian_repo-v5
python3 -m http.server 8080 --directory public &
```

使用 Chrome headless 或 puppeteer 截取 375x812 视口下的首页与目录页，确认：
- 目录图标可见
- 点击后抽屉展开
- 分类卡片正常显示

---

## Task 6: 推送 v5 分支

- [ ] **Step 1: 推送 v5**

```bash
cd /d/asxiuxiu_obsidian_repo-v5
git push origin v5
```

- [ ] **Step 2: 确认远端 v5 分支包含最新提交**

```bash
git log origin/v5 --oneline -5
```

---

## Task 7: 推送 main 分支

- [ ] **Step 1: 推送 main**

```bash
cd /d/asxiuxiu_obsidian_repo
git push origin main
```

- [ ] **Step 2: 确认 GitHub Actions 已触发**

在浏览器或 gh CLI 中查看 Actions 运行状态：

```bash
gh run list --workflow="Deploy Notes to GitHub Pages" --limit 5
```

---

## Task 8: 线上验证

- [ ] **Step 1: 等待部署完成**

GitHub Pages 部署通常需要 1-3 分钟。可通过以下命令轮询：

```bash
until curl -s -o /dev/null -w "%{http_code}" "https://asxiuxiu.github.io/asxiuxiu_obsidian_repo/" | grep -q "200"; do
  sleep 10
done
```

- [ ] **Step 2: 移动端截图验证**

使用 Chrome headless 对线上站点截取 375x812 视口：

```bash
npx puppeteer-core --... # 或自定义脚本
```

确认：
- 首页出现分类卡片
- 顶部或底部出现目录/菜单入口
- 点击目录入口可展开顶层分类
- 目录链接点击后返回 200

- [ ] **Step 3: 抽样检查目录链接**

```bash
for url in \
  "https://asxiuxiu.github.io/asxiuxiu_obsidian_repo/notes/c++%E7%BC%96%E7%A8%8B/" \
  "https://asxiuxiu.github.io/asxiuxiu_obsidian_repo/notes/%E8%AE%A1%E7%AE%97%E6%9C%BA%E5%9B%BE%E5%BD%A2%E5%AD%A6/" \
  "https://asxiuxiu.github.io/asxiuxiu_obsidian_repo/notes/%E7%B4%A2%E5%BC%95/"; do
  curl -s -o /dev/null -w "%{http_code} %{url_effective}\n" "$url"
done
```

Expected: all `200`.

---

## 自审检查

- **Spec coverage:**
  - 移动端目录图标可见 → Task 1 + Task 3 Step 1
  - 快速跳转 → Task 1（默认展开）+ Task 4（首页卡片）+ Task 3 Step 1（汉堡按钮可见）
  - 404 感知降低 → Task 1 + 首页直接入口减少依赖 Explorer
  - 设计优化 → Task 3 Step 4/5 + Task 4
  - 推送两个分支 → Task 6 + Task 7
- **Placeholder scan:** 无 TBD/TODO/"稍后实现"。
- **Type consistency:** 仅 SCSS 与 YAML 配置，无跨任务类型冲突。

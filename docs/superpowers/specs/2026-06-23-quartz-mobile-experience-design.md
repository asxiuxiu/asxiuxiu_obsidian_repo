# Quartz 移动端体验优化设计

## 现状与问题诊断

### 1. 目录 404 问题

已抽样检查远端站点 56 个目录索引页 + 50 个随机页面，HTTP 状态均为 200。`sitemap.xml` 与 `contentIndex.json` 条目数一致（776）。因此服务器端文件本身存在，404 更可能来自 **SPA 客户端路由失败** 或 **用户无法找到目录入口** 后的主观描述。

潜在根因：
- Explorer 目录树完全依赖客户端从 `static/contentIndex.json` 异步构建；若 JS 延迟或 contentIndex 与部署产物不同步，点击目录会进入 Quartz 的客户端 404 页面。
- 移动端汉堡菜单按钮默认 `hide-until-loaded`，在 JS 未加载前不可见，用户可能误以为"没有目录"。

### 2. 移动端目录图标不可见

- 移动端 Explorer 被折叠为 34px 宽的汉堡按钮（`.mobile-explorer`），且初始状态 `hide-until-loaded`。
- 展开后为全屏侧滑抽屉，需要二次点击才能看到目录树，不符合"快速在不同目录笔记间跳转"的诉求。

### 3. 视觉过于简约

- 当前 `custom.scss` 几乎为空，使用 Quartz 默认样式。
- 首页仅有一段文字 + 一个 wikilink，没有直观的分类入口。

## 设计目标

1. **移动端目录随时可达**：汉堡按钮默认可见；提供备用/增强导航。
2. **降低 404 感知**：确保目录链接可预测；在客户端增加更明确的空态/错误提示。
3. **提升视觉与信息层级**：参考数字花园/知识库常见设计，增加首页分类卡片、搜索视觉权重、移动端底部固定导航。

## 方案

### 方案 A：保守修复（推荐）

只修当前最痛的点，改动最小：
- 覆盖 `.mobile-explorer.hide-until-loaded` 为始终可见，避免 JS 未加载时目录入口消失。
- 覆盖 `.explorer.collapsed` 与 `.explorer-content`，让移动端抽屉默认展开或更易触发。
- 增加少量自定义样式让搜索、标题在移动端更醒目。

### 方案 B：增强导航

在 A 的基础上增加：
- 首页 `index.md` 改为分类卡片网格，快速进入 C++、图形学、操作系统等主目录。
- 移动端底部固定导航栏（Toolbar），放置"目录"、"搜索"、"首页"、"深色模式"图标。
- 优化 Explorer 抽屉动画与遮罩，提升打开/关闭的确定性。

### 方案 C：全量重构

重写布局与主题，参考 Notion/Docsify 式侧边栏，工作量与风险大，不推荐。

**本设计采用方案 B**：既解决痛点，又提供明显可感知的体验提升，改动仍集中在 `v5` 分支的 `custom.scss`、`quartz.ts`、`quartz.config.yaml` 和 `main` 分支的 `index.md`。

## 具体改动

### v5 分支

1. **`quartz/styles/custom.scss`**
   - 移动端 `.mobile-explorer` 始终可见，增大点击区域与对比度。
   - `.explorer.collapsed` 在移动端不再压缩为 34px，改为显示完整标题行；抽屉默认高度合理。
   - 增加 `.mobile-bottom-nav` 底部固定导航条样式（仅移动端）。
   - 调整首页 `.page-listing`、搜索框、文章标题在移动端的间距与字体。
   - 增加分类卡片样式（`.category-grid`、`.category-card`）。

2. **`quartz.config.yaml`**
   - 为 Explorer 设置 `folderDefaultState: open`，让桌面端目录默认展开；移动端通过 CSS 控制折叠。
   - 调整左侧 sidebar 组件顺序：page-title、search、explorer、darkmode、reader-mode。
   - 首页布局保持 content-page，不做大幅调整。

3. **`quartz.ts`**
   - 保持现有 Explorer / folder-page 排序逻辑。
   - 可选：通过 `componentRegistry.setOptionOverrides` 为 Explorer 设置 `title: "目录"` 与中文 aria-label 提升可访问性。

4. **新增脚本/组件（如需要）**
   - 若底部导航需要自定义交互，可在 `.quartz/plugins` 外通过 custom.scss + 少量内联 JS 实现；保持不修改已安装插件源码，避免升级冲突。

### main 分支

1. **`.github/workflows/deploy-notes.yml`**
   - 当前 workflow 会触发 `npx quartz plugin install` 和 `npx quartz build`。确认无需改动，保持现状。
   - 若新增首页分类卡片需要依赖 `index.md` 内容，则修改 workflow 中生成 `index.md` 的 heredoc。

2. **`index.md`**
   - 在 workflow 生成 `index.md` 的步骤中，写入分类卡片网格，链接到各大目录索引页。

## 验收标准

- [ ] 移动端宽度 375px 下，首页出现可见的目录/菜单入口。
- [ ] 点击目录入口后能在 1 次交互内看到顶层分类（C++、图形学、操作系统、构建系统等）。
- [ ] 目录中的文件夹链接点击后不出现 Quartz 客户端 404。
- [ ] 首页呈现分类卡片，提升信息层级。
- [ ] v5 与 main 两个分支均成功 push，GitHub Actions 部署成功。

## 风险

- 自定义 CSS 可能和 Quartz 默认样式冲突，需通过本地精简构建验证。
- 底部固定导航会占用视口高度，需确保不遮挡正文底部。

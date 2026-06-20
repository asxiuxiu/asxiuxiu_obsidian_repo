import { loadQuartzConfig, loadQuartzLayout } from "./quartz/plugins/loader/config-loader"
import * as ExternalPlugin from "./.quartz/plugins"
import { componentRegistry } from "./quartz/components/registry"

// 取节点的有效排序权重：文件用自身的 order；文件夹递归取子文件最小 order。
const getEffectiveOrder = (node: any): number => {
  if (!node.isFolder && node.data?.order != null) {
    return Number(node.data.order)
  }
  if (node.isFolder && node.children) {
    let min = Number.MAX_SAFE_INTEGER
    for (const child of node.children) {
      min = Math.min(min, getEffectiveOrder(child))
    }
    return min
  }
  return Number.MAX_SAFE_INTEGER
}

// Explorer 侧边栏按 frontmatter `order` 排序，无 order 的退回到按名称字母序
ExternalPlugin.Explorer({
  sortFn: (a, b) => {
    const orderA = getEffectiveOrder(a)
    const orderB = getEffectiveOrder(b)
    if (orderA !== orderB) return orderA - orderB
    return a.displayName.localeCompare(b.displayName, "zh-CN")
  },
})

// 文件夹列表页中的文件也按 order 排序；子文件夹因插件限制仍按字母序。
componentRegistry.setOptionOverrides("folder-page", {
  sort: (a: any, b: any) => {
    const orderA = a.frontmatter?.order ?? Number.MAX_SAFE_INTEGER
    const orderB = b.frontmatter?.order ?? Number.MAX_SAFE_INTEGER
    if (orderA !== orderB) return orderA - orderB
    const titleA = a.frontmatter?.title ?? ""
    const titleB = b.frontmatter?.title ?? ""
    return titleA.localeCompare(titleB, "zh-CN")
  },
})

const config = await loadQuartzConfig()
export default config
export const layout = await loadQuartzLayout()

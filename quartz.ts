import { loadQuartzConfig, loadQuartzLayout } from "./quartz/plugins/loader/config-loader"
import * as ExternalPlugin from "./.quartz/plugins"
import { componentRegistry } from "./quartz/components/registry"

// Explorer 侧边栏按 frontmatter `order` 排序，无 order 的退回到按名称字母序。
// 注意：sortFn 会被序列化到浏览器端 eval 执行，因此：
// 1. 不能引用外部作用域的函数/变量；
// 2. 不能定义命名函数或把箭头函数赋值给变量，否则 esbuild 的 keepNames
//    会注入 __name 辅助函数，导致客户端 ReferenceError。
// 下面的逻辑完全用原始循环/条件内联实现。
ExternalPlugin.Explorer({
  title: "目录",
  sortFn: (a, b) => {
    let orderA = Number.MAX_SAFE_INTEGER
    const stackA = [a]
    while (stackA.length > 0) {
      const cur = stackA.pop()
      if (!cur.isFolder && cur.data && cur.data.order != null) {
        const val = Number(cur.data.order)
        if (val < orderA) orderA = val
      }
      if (cur.isFolder && cur.children) {
        for (const child of cur.children) stackA.push(child)
      }
    }

    let orderB = Number.MAX_SAFE_INTEGER
    const stackB = [b]
    while (stackB.length > 0) {
      const cur = stackB.pop()
      if (!cur.isFolder && cur.data && cur.data.order != null) {
        const val = Number(cur.data.order)
        if (val < orderB) orderB = val
      }
      if (cur.isFolder && cur.children) {
        for (const child of cur.children) stackB.push(child)
      }
    }

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

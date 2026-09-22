# DSH Desktop 启动性能诊断

> 本文记录一次真实的性能排查过程：应用"重启特别卡"这件事，如何从一句模糊的主观感受，被拆解成可测量、可归因、可修复的具体结论。

## 一、先搞清楚"卡"发生在哪一段

"重启很卡"这句话本身没有可操作性。DSH Desktop 的重启其实是由**三个完全不同的进程阶段**拼起来的，它们的成因、优化手段、责任方都不一样：

```mermaid
graph LR
    A["Electron 主进程<br/>launchHarness()"] --> B["Harness Node 子进程<br/>runCli() 启动"]
    B --> C["Renderer 窗口<br/>加载前端资源"]
    C --> D["首屏可用"]
```

如果不知道慢在哪一段，所有优化都是猜测。所以第一步不是改代码，而是**找到那把已经存在的尺子**。

## 二、日志里已经内置了秒表

DSH Desktop 的主进程在启动时会写一份日志：

```
C:\Users\<你>\AppData\Roaming\dsh-desktop\logs\harness.log
```

关键点在于：**每一行都带 `+<ms>` 相对时间戳**。这不是普通的带绝对时间的日志——它从 `launch requested` 那一刻起算，所以任意两行相减就是那个阶段的真实耗时。

源码里的注释也明确说明了这个设计意图（`out/main/index.js` 的 `HarnessRuntime.launchClock` 字段）：

> Wall clock for the current launch. Every log line carries `+<ms>` from it, so a slow start can be attributed to a phase instead of guessed at.

也就是说，**这个功能就是为排查"启动慢"而设计的**。我们不需要自己插桩。

### 关键锚点

一次典型重启的骨架长这样：

| 日志锚点 | 含义 |
|---------|------|
| `launch requested (web profile)` | 时间原点 t=0 |
| `splash shown` | 启动图出现 |
| `previous Harness stopped` | 旧进程已被 SIGTERM 收回 |
| `profile maintenance done` | **profile 维护阶段结束** |
| `Bundled Node.js Harness process started` |  Node 子进程已 spawn |
| `invoking DSH runCli()` | Harness 开始真正初始化 |
| `dsh web: http://127.0.0.1:43129/...` | 端口已监听 |
| `Harness is ready` | **真正可用**（splash 消失的那一刻） |

## 三、提取指标：一条 PowerShell 命令

不要手动翻日志。用这段脚本把每次重启的两个关键耗时算出来：

```powershell
$l = "$env:APPDATA\dsh-desktop\logs\harness.log"
$t = Get-Content $l
$idx = 0..($t.Count-1) | Where-Object { $t[$_] -match 'launch requested' }
foreach ($i in $idx) {
  $seg = $t[$i..([math]::Min($i+40, $t.Count-1))]
  $maint = $seg | Select-String 'profile maintenance done' | Select-Object -First 1
  $ready = $seg | Select-String 'Harness is ready'          | Select-Object -First 1
  if ($maint -and $ready) {
    $m = [int][regex]::Match($maint.Line,'\+\s*(\d+)ms').Groups[1].Value
    $r = [int][regex]::Match($ready.Line,'\+\s*(\d+)ms').Groups[1].Value
    "{0} | 维护 {1,6}ms | Harness 启动 {2,6}ms | 总计 {3,6}ms" -f `
      $t[$i].Substring(1,19), $m, ($r-$m), $r
  }
}
```

按天聚合，就能看出趋势：

```powershell
# 只看 runCli -> ready 这段（真正的 Harness 启动成本）
$rows = @()
foreach ($i in $idx) {
  $seg = $t[$i..([math]::Min($i+40, $t.Count-1))]
  $cli   = $seg | Select-String 'invoking DSH runCli' | Select-Object -First 1
  $ready = $seg | Select-String 'Harness is ready'    | Select-Object -First 1
  if ($cli -and $ready) {
    $c = [int][regex]::Match($cli.Line,  '\+\s*(\d+)ms').Groups[1].Value
    $r = [int][regex]::Match($ready.Line,'\+\s*(\d+)ms').Groups[1].Value
    $rows += [pscustomobject]@{
      Date    = $t[$i].Substring(1,10)
      BootSec = [math]::Round(($r-$c)/1000,1)
    }
  }
}
$rows | Group-Object Date | ForEach-Object {
  "{0}  n={1,-3} avg={2,5:N1}s  max={3,5:N1}s" -f $_.Name, $_.Count,
    ($_.Group | Measure-Object BootSec -Average).Average,
    ($_.Group | Measure-Object BootSec -Maximum).Maximum
}
```

## 四、实测结论：这不是错觉，是真实回归

在真实机器上跑出来的数据：

| 日期 | 重启次数 | Harness 启动均值 | 峰值 |
|------|---------|-----------------|------|
| 09-17 | 19 | 6.1s | 9.7s |
| 09-18 | 39 | 5.8s | 15.6s |
| 09-20 | 12 | 5.8s | 7.7s |
| 09-21 | 11 | 7.0s | 8.3s |
| **09-22** | **14** | **12.3s** | **22.1s** |

**结论：Harness 启动耗时从稳定的 5.8s 翻倍到 12.3s，翻倍点就在 09-22。** 用户"感觉卡"的感受是准确的。

### 排除掉的假设

排查的价值一半在于排除。以下几项经过验证**不是**原因：

| 假设 | 验证方法 | 结论 |
|------|---------|------|
| V8 编译缓存失效 | 检查 `harness/cache/compile-cache` | ❌ 40.9MB / 4587 文件，且时间戳紧跟最后一次启动，缓存**健康** |
| Node 进程本身启动慢 | 直接计时 `node.exe -e "process.exit(0)"` | ❌ 161ms，可忽略 |
| 磁盘 I/O 卡在 profile 维护 | 看 `profile maintenance done` 耗时 | ❌ 通常 200-700ms，不是瓶颈 |
| 内存不足/进程泄漏 | 检查各进程 WorkingSet | ⚠️ 有 6 个进程、最大 397MB，但不是启动慢的主因 |

### 真正的瓶颈：`runCli()` → 端口监听

拆开单次启动（09-22 10:16:55 那次）：

```
+  404ms  starting
+  428ms  Bundled Node.js Harness process started   ← spawn 只花 24ms
+  529ms  invoking DSH runCli()                     ← 模块加载只花 101ms
+ 5443ms  waiting for Harness (5s)                  ← 空转
+10465ms  waiting for Harness (10s)                 ← 空转
+12541ms  pnpm shim written
+14123ms  dsh web: http://127.0.0.1:43129/         ← 13.6 秒黑洞在这一段
+14164ms  Harness is ready
```

**Node 进程 24ms 就起来了，模块加载 101ms 就完成了，但 `runCli()` 到端口监听之间花了 13.6 秒。**

这一段在做什么？源码显示，Harness 在监听端口之前要先解析并实例化整个 **profile bundle**——也就是 `cordis.yml` 里那一长串插件。这台机器上装了 9 个 bundle：

```
@deepseek-ai/dsh-base, @deepseek-ai/dsh-web-app, dsh-quick-open, dshmarket,
@linxin666/dsh-remote-web-ui, @liustack/modlens, dsh-better-sidebar,
dsh-context, dsh-inline-diff
```

其中 5 个是**第三方插件**，且通过 `link:` 指向 `.generations/live/` 下的独立安装代（generation），跨目录软链接在 Windows 上解析很慢。这就是 09-22 前后新增的变量。

## 五、可用的监测/优化手段

### 1. 用 Node 自带 profiler 抓 CPU 火焰图

主进程 spawn Harness 子进程时，环境变量是从父进程继承的（`buildHarnessSpawnOptions` 里 `...parentEnvironment`）。所以可以从 Electron 主进程这一侧注入：

```powershell
# 先完全退出 DSH Desktop，然后在同一终端里设置环境变量再启动
$env:NODE_OPTIONS = "--cpu-prof --cpu-prof-dir=$env:TEMP\dsh-prof"
& "D:\dsh_desktop\DSH Desktop\DSH Desktop.exe"
```

启动完成后，`$env:TEMP\dsh-prof` 下会生成 `.cpuprofile`，用 Chrome DevTools 的 Performance 面板加载即可看到火焰图。

> ⚠️ 注意：`--cpu-prof` 会显著拖慢启动，只用于定位，不要常开。另外它作用于**所有** Node 子进程，profile 文件名会混在一起。

### 2. 逐插件二分定位

既然瓶颈是插件初始化，最快的定位方式是**二分**。编辑：

```
%APPDATA%\dsh-desktop\harness\profiles\web\package.json
```

在 `dsh.profile.bundles` 数组里注释掉一半第三方插件（保留 `dsh-base` 和 `dsh-web-app`），重启并记录 `runCli -> ready` 耗时。若耗时腰斩，说明问题在被去掉的那一半里；否则在保留的那一半里。逐步收敛到单个插件。

这才是**成本最低、结论最硬**的方法——因为每次测量都是同一把尺子（日志里的 `+ms`）。

### 3. 检查软链接解析开销

```powershell
$p = "$env:APPDATA\dsh-desktop\harness\profiles\web\node_modules"
Get-Item $p\* -Force | Where-Object { $_.LinkType } |
  Select-Object Name, LinkType, Target
```

大量 `link:` 指向 `.generations/live/` 的符号链接，每个都要在一次冷启动中被解析。若确实很多，可以考虑把稳定版本固定为直接依赖（而非 generation 覆盖）。

### 4. 减少重启次数（治本）

从日志能看出一个更根本的问题：**09-22 一天重启了 14 次**。每次重启都要付出 12 秒。与其优化单次启动，不如先问"为什么一天要重启 14 次"——如果是插件安装/更新触发的自动重启，那么把插件更新集中到一次、避免在会话中途装卸插件，收益可能比任何微优化都大。

## 六、这套方法的可迁移性

真正值得记住的不是"DSH 启动慢"，而是这个排查范式：

1. **找已有埋点**：成熟软件的性能问题，通常日志里已经有秒表。先找 `+<ms>` 这类相对时间戳，别急着上 profiler。
2. **切分阶段再归因**：把"慢"拆成进程生命周期上的若干段，测量每一段，而不是笼统地感觉。
3. **建立时间序列基线**：单次测量说明不了问题，按天聚合才能看出"从哪天开始变慢"——这一步直接把嫌疑范围锁定到了 09-22 的变更。
4. **排除法与二分法优先**：在插桩之前，先用已有的测量手段二分，成本低且结论硬。
5. **区分"单次优化"与"减少次数"**：14 次 × 12 秒 = 168 秒/天。先减少次数，再优化单次。

## 相关笔记

> [[Notes/构建系统/索引|← 返回 构建系统索引]]

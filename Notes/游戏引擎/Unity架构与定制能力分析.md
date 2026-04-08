> [[索引|← 返回 游戏引擎索引]]

# Unity 架构与定制能力分析

## Why：为什么要了解Unity的架构边界？

作为商业引擎中使用最广泛的选择，Unity的**分层架构**决定了：
- 哪些部分可以深度定制（渲染管线、ECS框架）
- 哪些部分是黑盒（物理、音频底层）
- 源码获取的权限门槛

明确这些边界，才能正确评估Unity是否适合你的技术需求。

---

## What：Unity的真实架构分层

```
┌─────────────────────────────────────────────────────────────┐
│ 用户层（你的代码）                                             │
│  - C# 游戏脚本                                                │
│  - ShaderLab/HLSL 着色器                                       │
├─────────────────────────────────────────────────────────────────┤
│ 托管包装层（UnityEngine.dll）                                  │
│  - C# API 封装                                                  │
│  - 通过 Internal Call 调用 C++ 核心                             │
│  - 源码：参考级（GitHub UnityCsReference）                      │
├─────────────────────────────────────────────────────────────────┤
│ 包层（Packages）                                               │
│  - SRP（URP/HDRP）- ✅ 开源可修改                               │
│  - DOTS（Entities/Burst）- ✅ 开源可修改                         │
│  - UI Toolkit - ✅ 开源可修改                                    │
├─────────────────────────────────────────────────────────────────┤
│ Runtime核心层（C/C++）- ❌ 完全封闭                             │
│  - 渲染底层（DirectX/Metal/Vulkan抽象）                          │
│  - 物理引擎（PhysX）                                             │
│  - 音频系统（FMOD/自研）                                         │
│  - 动画系统（C++核心）                                           │
│  - 输入系统                                                      │
│  - 文件系统/资源管理                                             │
└─────────────────────────────────────────────────────────────┘
```

---

## 各层定制权限详解

### ✅ 可深度定制（C# 开源层）

#### 1. Scriptable Render Pipeline (SRP)

**权限**：Personal/Pro 订阅即可完全定制

**定制方式**：
```csharp
// 方式1：从零创建自定义 SRP
public class MyRenderPipeline : RenderPipeline {
    protected override void Render(
        ScriptableRenderContext context, 
        Camera[] cameras
    ) {
        foreach (var camera in cameras) {
            // 完全控制：剔除、光照、绘制、后处理
            CullingResults cullResults;
            CullResults.Cull(camera, context, out cullResults);
            
            var drawSettings = new DrawRendererSettings(
                camera, 
                new ShaderPassName("MyPass")
            );
            context.DrawRenderers(
                cullResults.visibleRenderers, 
                ref drawSettings
            );
        }
    }
}
```

**方式2：修改 URP/HDRP 源码**
```bash
# 1. 从 PackageCache 复制到项目
mkdir -p Packages/com.unity.render-pipelines.universal
cp -r Library/PackageCache/com.unity.render-pipelines.universal/* \
   Packages/com.unity.render-pipelines.universal/

# 2. 直接修改源码（如 ForwardRenderer.cs）
# 3. Unity 自动使用项目内的包版本
```

**实际限制**：
- 底层 GPU 资源分配（Texture上传格式、内存管理）仍是黑盒
- 如需修改这些，需 Enterprise 源码访问

#### 2. DOTS 框架

**包含包**：
- `com.unity.entities`（ECS核心）
- `com.unity.burst`（编译器）
- `com.unity.jobs`（并行任务）

**定制能力**：
```csharp
// 可以完全修改 Entities 包的 C# 层
// 例如：自定义 System 调度逻辑、修改 Component 存储方式
```

**封闭部分**：
- Burst 编译器的 LLVM 后端（C++）
- Job System 的线程池调度（C++）

---

### ❌ 不可定制（C++ 封闭核心）

| 模块 | C# 层 | C++ 核心 | 能否修改底层 |
|------|-------|---------|-------------|
| **物理系统** | `UnityEngine.Physics` | PhysX 集成 | ❌ 不可修改碰撞检测算法 |
| **音频系统** | `AudioSource` | FMOD/自研核心 | ❌ 不可修改混音器底层 |
| **动画系统** | `Animator` | C++ 骨骼动画 | ❌ 不可修改动画混合逻辑 |
| **粒子系统** | `ParticleSystem` | C++ 模拟核心 | ❌ 不可修改粒子物理 |
| **输入系统** | `InputSystem` | 平台原生抽象 | ❌ 不可修改设备驱动层 |

**替代方案**：
- 通过 **Native Plugin** (P/Invoke) 接入自研 C++ 模块
- 但集成度有限，无法深度替换引擎原生系统

---

## 源码获取权限分层

| 订阅级别 | 价格 | C++ Runtime 源码 | IL2CPP 源码 | 可修改并发布定制版本 |
|----------|------|------------------|-------------|---------------------|
| **Personal** | 免费 | ❌ | ❌ | ❌ |
| **Plus** | ~$400/年 | ❌ | ❌ | ❌ |
| **Pro** | ~$2,000/年 | ❌ | ❌ | ❌ |
| **Enterprise** | $10万+/年 | ✅ 只读 | ✅ 需额外NDA | ❌ |
| **Enterprise + ISS** | 更高 | ✅ 可修改 | ✅ 可修改 | ✅ |

> **关键事实**：Unity 源码**不能单独购买**，必须升级整个订阅级别。

---

## 与 Unreal Engine 的对比

| 维度 | Unity | Unreal Engine |
|------|-------|---------------|
| **源码获取门槛** | 需 Enterprise（$10万+/年） | 免费注册即可 |
| **授权模式** | 封闭源码，订阅制 | 开源，收入超$100万后5%分成 |
| **可定制深度** | 渲染/ECS可改，核心系统黑盒 | 全部 C++ 源码开放 |
| **编译速度** | C# 秒级 | C++ 5-30分钟 |
| **调试体验** | Attach 调试，无法单步进引擎 | 可单步调试引擎每一行 |

---

## How：Unity 定制实战

### 场景1：自定义渲染效果（无需源码）

```csharp
// 使用 RenderFeature 注入自定义 Pass
public class CustomRenderFeature : ScriptableRendererFeature {
    public override void AddRenderPasses(
        ScriptableRenderer renderer, 
        ref RenderingData renderingData
    ) {
        // 在 URP 渲染流程中插入自定义 Pass
        renderer.EnqueuePass(new CustomPass());
    }
}

public class CustomPass : ScriptableRenderPass {
    public override void Execute(
        ScriptableRenderContext context, 
        ref RenderingData renderingData
    ) {
        // 完全自定义绘制逻辑
        CommandBuffer cmd = CommandBufferPool.Get("CustomPass");
        cmd.DrawProcedural(...);  // 自定义绘制
        context.ExecuteCommandBuffer(cmd);
    }
}
```

### 场景2：修改 URP 源码

```bash
# 步骤1：克隆 URP 包到项目
cd Packages
git clone https://github.com/Unity-Technologies/Graphics.git

# 步骤2：修改 UniversalRenderPipeline.cs
# 例如：添加自定义剔除逻辑

# 步骤3：Unity 自动检测并使用本地包
```

### 场景3：需要修改物理底层（无法做到）

**需求**：替换 PhysX 为自研物理引擎

**Unity 的限制**：
- `UnityEngine.Physics` 直接绑定到 PhysX，无抽象接口
- 无法通过 Package 替换底层实现

**可行方案**：
1. 关闭 Unity 物理（`Physics.autoSimulation = false`）
2. 通过 Native Plugin 接入自研物理
3. 每帧同步 Transform 数据

```csharp
// 自研物理驱动
void FixedUpdate() {
    // 1. 将 Unity Transform 传给 C++ 物理
    NativePhysics.Update(unityEntities);
    
    // 2. 获取物理结果
    var results = NativePhysics.GetResults();
    
    // 3. 回写 Unity Transform
    foreach (var result in results) {
        result.transform.position = result.physicsPosition;
    }
}
```

**代价**：
- 数据拷贝开销
- 无法使用 Unity 物理相关的组件和工具
- 调试困难

---

## 决策建议

| 你的需求 | Unity 能否满足 | 建议 |
|----------|---------------|------|
| 自定义渲染管线 | ✅ 完全可以 | 使用 SRP，无需 Enterprise |
| 深度修改 ECS | ✅ 可以 | DOTS 包开源 |
| 修改物理底层 | ❌ 不能 | 考虑 UE（源码开放）或自研引擎 |
| 修改音频底层 | ❌ 不能 | 使用 Wwise/FMOD 中间件 |
| 单步调试引擎 | ❌ 不能 | 需 Enterprise，或转 UE |
| 源码级性能优化 | ⚠️ 部分可以 | 仅渲染/ECS层，核心系统黑盒 |

---

## 总结

Unity 的架构策略是**分层开放**：
- **渲染和逻辑层**（SRP、DOTS）完全开放，满足大多数定制需求
- **核心系统**（物理、音频、动画）封闭，确保跨平台稳定性

对于大多数开发者，**Personal/Pro 订阅的定制能力已足够**。只有在需要修改底层物理/渲染核心时，才需要考虑 Enterprise 或转用 Unreal Engine。

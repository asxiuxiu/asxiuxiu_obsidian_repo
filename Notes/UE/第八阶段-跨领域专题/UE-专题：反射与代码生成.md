---
title: UE-专题：反射与代码生成
date: 2026-04-19
tags:
  - ue-source
  - reflection
  - uht
  - blueprint-vm
  - codegen
aliases:
  - UE反射与代码生成
---

> [[00-UE全解析主索引|← 返回 00-UE全解析主索引]]

---

## Why：为什么要理解反射与代码生成？

UE 的反射系统不是 C++ 语言层面的反射（RTTI），而是一套**自行设计的、贯穿编译期与运行期的元数据体系**。理解它的必要性在于：

1. **编辑器可扩展性**：PropertyEditor、Details 面板、ContentBrowser 的右键菜单都依赖 UProperty 枚举来动态生成 UI
2. **蓝图互操作**：蓝图的节点图最终编译为字节码，虚拟机执行时通过 UFunction 查找、UProperty 偏移访问内存
3. **序列化与网络复制**：SaveGame、Cook、网络 RPC 都依赖反射元数据来遍历对象字段
4. **垃圾回收**：GC 通过 UClass 的 `RefLink` 链找到所有 UObject* 成员，建立引用图
5. **热重载**：LiveCoding 替换 DLL 后，需要重新匹配新旧 UClass 的字段偏移

如果没有这套系统，UE 编辑器就无法"理解" C++ 类的结构，蓝图也无法调用 C++ 函数。

---

## What：反射与代码生成的核心组成

### 1. 整体架构图

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              编译期 (Compile Time)                            │
├─────────────────────────────────────────────────────────────────────────────┤
│  C++ 头文件 (.h)                                                            │
│  ├── UCLASS() / UPROPERTY() / UFUNCTION() / USTRUCT() / UINTERFACE()       │
│  └── GENERATED_BODY()  ← 占位符，由 UHT 展开                                │
│                                                                             │
│  UHT (UnrealHeaderTool)                                                     │
│  ├── 解析器 (UhtClassParser/UhtPropertyParser/UhtFunctionParser)            │
│  ├── 代码生成器 (UhtHeaderCodeGeneratorHFile/UhtHeaderCodeGeneratorCppFile)│
│  └── 输出：.generated.h + .gen.cpp                                          │
├─────────────────────────────────────────────────────────────────────────────┤
│                              运行期 (Runtime)                                 │
├─────────────────────────────────────────────────────────────────────────────┤
│  .generated.h 展开为：                                                       │
│  ├── 友元声明 (Z_Construct_UClass_XXX)                                      │
│  ├── 静态函数声明 (StaticClass, 构造函数模板)                                │
│  └── 属性偏移存取器 (用于编辑器属性面板)                                      │
│                                                                             │
│  .gen.cpp 提供：                                                             │
│  ├── Z_Construct_UClass_XXX()  → 延迟注册到 UObject 系统                    │
│  ├── 属性元数据表 (FPropertyParams)                                         │
│  └── 函数参数表 (FFunctionParams)                                           │
│                                                                             │
│  反射对象层级：                                                              │
│  UObject → UField → UStruct → UClass                                        │
│         → UProperty (FProperty*)                                            │
│         → UFunction                                                         │
│         → UEnum / UScriptStruct                                             │
├─────────────────────────────────────────────────────────────────────────────┤
│                              蓝图层 (Blueprint Layer)                         │
├─────────────────────────────────────────────────────────────────────────────┤
│  蓝图编辑器 (Blueprint Editor)                                              │
│  ├── UK2Node 图 → FKismetCompilerContext::Compile()                         │
│  ├── 中间表示：FBlueprintCompiledStatement (KCST_*)                         │
│  └── FKismetCompilerVMBackend → TArray<uint8> Script (字节码)              │
│                                                                             │
│  蓝图 VM (ScriptCore.cpp)                                                   │
│  ├── UObject::ProcessEvent(UFunction*, void* Parms)                         │
│  ├── ProcessLocalScriptFunction() → while(Step()) 解释执行                  │
│  └── GNatives[EX_*] 分派表 (110+ 条字节码指令)                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 2. 关键数据结构

#### 2.1 反射对象继承链

```
UObject (CoreUObject/Class.h 第 58 行)
  └── UField (CoreUObject/Class.h 第 180 行)
        ├── UStruct (CoreUObject/Class.h 第 476 行)
        │     ├── UClass (CoreUObject/Class.h 第 3792 行)
        │     ├── UScriptStruct (CoreUObject/Class.h 第 1719 行)
        │     └── UFunction (CoreUObject/Class.h 第 2475 行)
        └── UEnum (CoreUObject/Class.h 第 2790 行)
```

#### 2.2 UClass 核心字段（运行时内存布局）

| 字段 | 类型 | 作用 |
|------|------|------|
| `ClassConstructor` | `ClassConstructorType` | C++ 构造函数指针，用于 NewObject |
| `ClassVTableHelperCtorCaller` | `ClassVTableHelperCtorCallerType` | 热重载时构造临时对象 |
| `ClassFlags` | `EClassFlags` | 类标志（Abstract、Config、Blueprintable 等） |
| `ClassCastFlags` | `EClassCastFlags` | 加速 `IsA()` 判断的位掩码 |
| `ClassWithin` | `TObjectPtr<UClass>` | 合法 Outer 类型限制 |
| `ClassReps` | `TArray<FRepRecord>` | 网络复制字段列表 |
| `NetFields` | `TArray<TObjectPtr<UField>>` | 网络相关函数字段 |

> 源码：`Engine/Source/Runtime/CoreUObject/Public/UObject/Class.h` 第 3792–3915 行

#### 2.3 UProperty → FProperty 的演进

UE4 早期使用 `UProperty`（继承自 `UField`，是 UObject），但每个属性都作为 UObject 分配带来了严重的内存和 GC 开销。UE4.25+ 引入 `FField` 体系：

```
// 旧体系 (UE4 早期)
UProperty : public UField  // 每个属性都是 UObject

// 新体系 (UE4.25+)
FField                     // 非 UObject，轻量级
  └── FProperty
        ├── FByteProperty
        ├── FIntProperty
        ├── FFloatProperty
        ├── FObjectProperty
        ├── FStructProperty
        ├── FArrayProperty
        ├── FMapProperty
        └── ... (30+ 种类型)
```

`UStruct::ChildProperties` 指向 `FProperty*` 链表（最派生 → 基类），`PropertyLink`/`RefLink`/`DestructorLink` 是运行时建立的快捷链表。

> 源码：`Engine/Source/Runtime/CoreUObject/Public/UObject/UnrealTypePrivate.h` 第 17 行起

#### 2.4 UFunction 核心字段

| 字段 | 类型 | 作用 |
|------|------|------|
| `FunctionFlags` | `EFunctionFlags` | Native/BlueprintCallable/Net 等标志 |
| `NumParms` | `uint8` | 参数总数 |
| `ParmsSize` | `uint16` | 参数总内存大小 |
| `ReturnValueOffset` | `uint16` | 返回值在参数块中的偏移 |
| `Func` | `FNativeFuncPtr` | C++ 原生函数指针 |
| `Script` | `TArray<uint8>` | 蓝图字节码（非 Native 函数） |

> 源码：`Engine/Source/Runtime/CoreUObject/Public/UObject/Class.h` 第 2475–2575 行

---

## How：从宏标记到 VM 执行的完整链路

### 链路一：UHT 代码生成（编译期）

#### Step 1：宏解析

当 C++ 编译器遇到以下代码时：

```cpp
UCLASS(Blueprintable, BlueprintType)
class MY_API AMyActor : public AActor
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Health;

    UFUNCTION(BlueprintCallable)
    void TakeDamage(int32 Amount);
};
```

`GENERATED_BODY()` 被展开为（来自 `ObjectMacros.h` 第 765 行）：

```cpp
#define GENERATED_BODY(...) BODY_MACRO_COMBINE(CURRENT_FILE_ID,_,__LINE__,_GENERATED_BODY);
```

这个宏拼接出唯一的标识符，例如 `FILE_ID_123_GENERATED_BODY`，其真实定义位于对应的 `.generated.h` 文件中。

#### Step 2：UHT 解析流程

```
UhtHeaderFileParser::Parse()           // 逐文件解析
  ├── UhtClassParser::Parse()          // 解析 UCLASS
  │     ├── UhtPropertyParser          // 解析 UPROPERTY
  │     └── UhtFunctionParser          // 解析 UFUNCTION
  ├── UhtScriptStructParser::Parse()   // 解析 USTRUCT
  └── UhtEnumParser::Parse()           // 解析 UENUM
```

> 源码：`Engine/Source/Programs/Shared/EpicGames.UHT/Parsers/`

#### Step 3：代码生成

UHT 为每个头文件生成两个产物：

| 产物 | 生成器 | 内容 |
|------|--------|------|
| `.generated.h` | `UhtHeaderCodeGeneratorHFile` | 类体内的宏展开、友元声明、静态函数原型 |
| `.gen.cpp` | `UhtHeaderCodeGeneratorCppFile` | `Z_Construct_UClass_XXX`、属性元数据、函数参数表 |

`.generated.h` 的典型内容（概念性）：

```cpp
#define FILE_ID_123_GENERATED_BODY \
    friend struct Z_Construct_UClass_AMyActor_Statics; \
    public: \
    static UClass* StaticClass(); \
    private: \
    // ... 属性偏移存取器等
```

`.gen.cpp` 的典型内容（概念性）：

```cpp
// 属性参数表
const UE::CodeGen::FPropertyParamsBase* const Z_PropertyTable[] = {
    new UE::CodeGen::FIntPropertyParams(...),
};

// 函数参数表
const UE::CodeGen::FFunctionParams Z_FunctionParams[] = {
    UE::CodeGen::FFunctionParams(...),
};

// 类构造
UClass* Z_Construct_UClass_AMyActor()
{
    static UClass* OuterSingleton = nullptr;
    if (!OuterSingleton)
    {
        OuterSingleton = new(EC_InternalUseOnlyConstructor, ...) 
            UClass(UE::CodeGen::FObjectParams(...), ...);
        InitializePrivateStaticClass(
            Z_Construct_UClass_AMyActor,
            Z_Construct_UClass_AActor(),  // Super
            OuterSingleton,
            Z_Construct_UClass_UObject(), // Within
            TEXT("/Script/MyModule"),
            TEXT("AMyActor")
        );
    }
    return OuterSingleton;
}

// 注册信息
static FClassRegistrationInfo Z_Registration_Info_UClass_AMyActor;
static const FClassRegisterCompiledInInfo Z_CompiledInDeferFile_FID_MyActor_h(...);
```

> 源码：`Engine/Source/Programs/Shared/EpicGames.UHT/Exporters/CodeGen/`

---

### 链路二：反射对象注册（启动期）

#### 核心问题：为什么需要延迟注册？

C++ 全局对象的构造顺序在不同编译单元之间是不确定的。如果 `AMyActor` 的 `UClass` 在 `AActor` 的 `UClass` 之前构造，就会出现父类未注册的问题。UE 采用**延迟注册（Deferred Registration）**解决：

#### 注册流程

```
1. 全局构造阶段（C++ 启动）
   └── IMPLEMENT_CLASS(AMyActor, ...)  // .gen.cpp 末尾的宏
       └── RegisterCompiledInInfo(...)  // 将 Z_Construct_UClass_AMyActor 注册到延迟队列

2. UObject 系统初始化阶段
   └── FEngineLoop::Init()
       └── UObjectProcessRegistrants()   // UObjectBase.cpp 第 614 行
           └── 遍历延迟队列，逐个调用 DeferredRegister()

3. 单个类注册
   └── UObjectBase::DeferredRegister()
       └── AddObject()                    // 加入全局 UObjectArray
           └── InitializePrivateStaticClass()  // Class.cpp 第 127 行
               └── 设置 SuperStruct、ClassWithin、ClassFlags
               └── 调用 RegisterDependencies() 确保父类先注册
               └── 建立 PropertyLink / RefLink / DestructorLink 链
```

> 源码：`Engine/Source/Runtime/CoreUObject/Private/UObject/UObjectBase.cpp` 第 614–664 行
> 源码：`Engine/Source/Runtime/CoreUObject/Private/UObject/Class.cpp` 第 127–155 行

#### 关键数据结构：延迟注册队列

```cpp
// UObjectBase.cpp 中的全局队列
TMap<UObjectBase*, FPendingRegistrantInfo> PendingRegistrants;

struct FPendingRegistrantInfo
{
    UClass* (*StaticClassFn)();  // 指向 StaticClass() 的函数指针
    const TCHAR* PackageName;
    const TCHAR* Name;
};
```

---

### 链路三：蓝图编译（编辑器期）

#### 编译入口

当用户在蓝图编辑器中点击 "Compile" 时：

```cpp
// KismetCompiler.cpp 第 5436 行
void FKismetCompilerContext::Compile()
{
    CompileClassLayout(EInternalCompilerFlags::None);   // 阶段一：布局
    CompileFunctions(EInternalCompilerFlags::None);     // 阶段二：函数
}
```

#### 阶段一：编译类布局（CompileClassLayout）

```cpp
// KismetCompiler.cpp 第 4732 行
void FKismetCompilerContext::CompileClassLayout(EInternalCompilerFlags InternalFlags)
{
    // 1. 确保 UBlueprintGeneratedClass 存在
    if (!TargetClass)
    {
        SpawnNewClass(NewGenClassName.ToString());
        Blueprint->GeneratedClass = TargetClass;
    }
    
    // 2. 节点早期验证
    for (UEdGraphNode* Node : AllNodes)
        Node->EarlyValidation(MessageLog);
    
    // 3. 创建变量属性（FProperty）
    for (FBPVariableDescription& Variable : Blueprint->NewVariables)
        CreatePropertyOnScope(NewClass, Variable.VarName, ...);
    
    // 4. 父类布局合并
    MergeComponentDataFromParent();
    
    // 5. 链接属性（计算偏移）
    NewClass->Link(...);
    
    // 6. 创建默认对象（CDO）
    NewClass->GetDefaultObject(true);
}
```

> 源码：`Engine/Source/Editor/KismetCompiler/Private/KismetCompiler.cpp` 第 4732–4810 行

#### 阶段二：编译函数（CompileFunctions）

```
CompileFunctions()
  ├── PrecompileFunction()        // 构建执行流图
  │     ├── 拓扑排序节点（LinearExecutionList）
  │     ├── 为每个节点创建 FNodeHandlingFunctor
  │     └── 处理纯函数（Pure Function）的数据流依赖
  │
  ├── CompileFunction()           // 生成中间表示
  │     └── 遍历 LinearExecutionList
  │           └── Node->CreateNodeHandler(this)->Compile(...)
  │                 └── 生成 FBlueprintCompiledStatement
  │
  └── PostcompileFunction()       // 后处理

FKismetCompilerVMBackend::GenerateCodeFromClass()
  └── ConstructFunction()
        ├── FScriptBuilderBase ScriptWriter
        └── 遍历 Statements → GenerateCodeForStatement()
              └── 写入 TArray<uint8> Script（字节码）
```

> 源码：`Engine/Source/Editor/KismetCompiler/Private/KismetCompiler.cpp` 第 4939–4983 行
> 源码：`Engine/Source/Editor/KismetCompiler/Private/KismetCompilerVMBackend.cpp` 第 2297–2314 行

#### 中间表示：FBlueprintCompiledStatement

```cpp
// BlueprintCompiledStatement.h
enum EKismetCompiledStatementType
{
    KCST_CallFunction = 1,        // 调用函数
    KCST_Assignment = 2,          // 赋值
    KCST_UnconditionalGoto = 4,   // 无条件跳转
    KCST_GotoIfNot = 6,           // 条件跳转
    KCST_Return = 7,              // 返回
    KCST_EndOfThread = 8,         // 线程结束
    KCST_DynamicCast = 14,        // 动态类型转换
    KCST_CreateArray = 22,        // 创建数组
    // ... 共 30+ 种类型
};

struct FBlueprintCompiledStatement
{
    EKismetCompiledStatementType Type;
    FBPTerminal* FunctionContext;     // 调用对象
    UFunction* FunctionToCall;        // 调用函数
    FBlueprintCompiledStatement* TargetLabel;  // 跳转目标
    FBPTerminal* LHS;                 // 左值（结果存储）
    TArray<FBPTerminal*> RHS;         // 右值（参数列表）
};
```

> 源码：`Engine/Source/Editor/KismetCompiler/Public/BlueprintCompiledStatement.h`

#### 字节码生成示例

假设蓝图中有简单的赋值节点：`Health = 100`

编译器生成：
1. `KCST_Assignment` 语句
2. VM Backend 将其翻译为字节码序列：
   ```
   EX_Let                        // 赋值操作
     FProperty* (Health)         // 目标属性
     EX_IntConst 100             // 源值
   EX_Nothing                    // 结束
   ```

---

### 链路四：蓝图 VM 执行（运行期）

#### 入口点

```cpp
// ScriptCore.cpp 第 2015 行
void UObject::ProcessEvent(UFunction* Function, void* Parms)
{
    // 1. 安全性检查（不可达对象、PostLoad 中不能调用脚本等）
    // 2. 网络调用空间判断（Local/Remote/Absorbed）
    // 3. 如果是 Native 函数，直接调用 Func 指针
    // 4. 如果是蓝图函数，进入 VM 执行
    
    // ... 栈帧设置 ...
    
    // 调用函数
    Function->Invoke(this, Stack, RESULT_PARAM);
}
```

> 源码：`Engine/Source/Runtime/CoreUObject/Private/UObject/ScriptCore.cpp` 第 2015–2078 行

#### VM 执行循环

```cpp
// ScriptCore.cpp 第 1186 行
void ProcessLocalScriptFunction(UObject* Context, FFrame& Stack, RESULT_DECL)
{
    UFunction* Function = Stack.Node;
    uint8 Buffer[MAX_SIMPLE_RETURN_VALUE_SIZE];
    
    // 主执行循环
    while (*Stack.Code != EX_Return && !Stack.bAbortingExecution)
    {
        Stack.Step(Stack.Object, Buffer);
    }
    
    // 处理返回值
    if (!Stack.bAbortingExecution)
    {
        Stack.Step(Stack.Object, RESULT_PARAM);
    }
}
```

> 源码：`Engine/Source/Runtime/CoreUObject/Private/UObject/ScriptCore.cpp` 第 1186–1265 行

#### Step 执行

```cpp
// ScriptCore.cpp 第 508 行
void FFrame::Step(UObject* Context, RESULT_DECL)
{
    // 读取下一条字节码指令
    const int32 B = *Code++;
    
    // 通过 GNatives 分派表调用对应的 exec 函数
    (GNatives[B])(Context, *this, RESULT_PARAM);
}
```

> 源码：`Engine/Source/Runtime/CoreUObject/Private/UObject/ScriptCore.cpp` 第 508–512 行

#### 字节码指令集（EExprToken）

```cpp
// Script.h 第 189 行起
enum EExprToken : uint8
{
    EX_LocalVariable      = 0x00,   // 局部变量
    EX_InstanceVariable   = 0x01,   // 实例变量
    EX_DefaultVariable    = 0x02,   // 类默认变量
    EX_Return             = 0x04,   // 返回
    EX_Jump               = 0x06,   // 跳转
    EX_JumpIfNot          = 0x07,   // 条件跳转
    EX_Let                = 0x0F,   // 赋值
    EX_ClassContext       = 0x12,   // 类默认对象上下文
    EX_Context            = 0x19,   // 对象上下文
    EX_VirtualFunction    = 0x1B,   // 虚函数调用
    EX_FinalFunction      = 0x1C,   // Final 函数调用
    EX_IntConst           = 0x1D,   // 整数常量
    EX_FloatConst         = 0x1E,   // 浮点常量
    EX_ObjectConst        = 0x20,   // 对象常量
    EX_DynamicCast        = 0x2E,   // 动态类型转换
    EX_CallMath           = 0x68,   // 纯数学函数调用
    EX_SwitchValue        = 0x69,   // Switch 表达式
    // ... 共 110+ 条指令
};
```

> 源码：`Engine/Source/Runtime/CoreUObject/Public/UObject/Script.h` 第 189–299 行

#### 函数调用示例

以 `EX_Context + EX_FinalFunction` 为例（调用对象上的 Final 函数）：

```cpp
// ScriptCore.cpp 第 3111 行
DEFINE_FUNCTION(UObject::execContext)
{
    // 1. 求值上下文对象（Stack.Step 计算对象表达式）
    UObject* NewContext;
    Stack.Step(Stack.Object, &NewContext);
    
    // 2. 如果上下文为 NULL，跳过函数体
    if (NewContext == NULL)
    {
        // 读取并跳过 CodeSkipSize 字节
        CodeSkipSizeType NumBytesToSkip;
        Stack.Step(Stack.Object, &NumBytesToSkip);
        Stack.Code += NumBytesToSkip;
        return;
    }
    
    // 3. 在上下文中执行后续字节码
    Stack.Step(NewContext, RESULT_PARAM);
}

// ScriptCore.cpp 第 3260 行
DEFINE_FUNCTION(UObject::execFinalFunction)
{
    // 读取 UFunction 指针
    UFunction* Function;
    Stack.ReadObjectPtr(Function);
    
    // 调用函数（Native 或 Script）
    Function->Invoke(Context, Stack, RESULT_PARAM);
}
```

> 源码：`Engine/Source/Runtime/CoreUObject/Private/UObject/ScriptCore.cpp` 第 3111–3265 行

#### 执行栈结构（FFrame）

```cpp
// Stack.h 第 113 行
struct FFrame : public FOutputDevice
{
    UFunction* Node;           // 当前执行的函数
    UObject* Object;           // 上下文对象（this）
    uint8* Code;               // 当前字节码指针
    uint8* Locals;             // 局部变量内存块
    
    FProperty* MostRecentProperty;
    uint8* MostRecentPropertyAddress;
    
    FlowStackType FlowStack;   // 执行流栈（用于 Latent 操作）
    FFrame* PreviousFrame;     // 调用者栈帧（链表）
    FOutParmRec* OutParms;     // Out 参数记录链
};
```

> 源码：`Engine/Source/Runtime/CoreUObject/Public/UObject/Stack.h` 第 113–157 行

---

## Interface 层：公共接口与模块边界

### UHT 模块边界

| 模块 | 职责 | 输入 | 输出 |
|------|------|------|------|
| `Programs/Shared/EpicGames.UHT` | 头文件解析与代码生成 | `*.h` (含 UCLASS/UPROPERTY) | `*.generated.h`, `*.gen.cpp` |
| `Runtime/CoreUObject` | 反射运行时 | `.gen.cpp` 的注册信息 | `UClass`/`UFunction`/`UProperty` 对象 |
| `Editor/BlueprintGraph` | 蓝图节点定义 | `UK2Node` 子类 | 节点图 (UEdGraph) |
| `Editor/KismetCompiler` | 蓝图编译器 | `UEdGraph` | `UBlueprintGeneratedClass` + 字节码 |

### 关键 .Build.cs 依赖

```
CoreUObject.Build.cs:
  - PrivateDependency: Core
  - PublicDependency: TraceLog
  
BlueprintGraph.Build.cs:
  - PrivateDependency: CoreUObject, Engine, UnrealEd
  
KismetCompiler.Build.cs:
  - PrivateDependency: CoreUObject, Engine, BlueprintGraph, UnrealEd
```

---

## Data 层：内存布局与对象生命周期

### 反射对象的内存开销对比

| 类型 | UObject 开销 | FField 开销 | 比例 |
|------|-------------|-------------|------|
| UProperty (旧) | ~120 bytes | - | 100% |
| FProperty (新) | - | ~40 bytes | ~33% |

一个拥有 50 个属性的类，旧体系需要 6000 bytes 的 UObject 开销，新体系只需 2000 bytes。

### UClass 对象的生命周期

```
1. 编译期：UHT 生成 Z_Construct_UClass_XXX 函数和元数据表
2. 启动期：C++ 全局构造 → 注册到延迟队列
3. 初始化期：UObjectProcessRegistrants() → DeferredRegister() → AddObject()
4. 运行期：通过 StaticClass() / FindObject<UClass>() 访问
5. 热重载期：LiveCoding 替换 DLL → 重新注册 → 旧类标记为废弃
6. 销毁期：GC 清理（UClass 通常全局存活到进程结束）
```

### 蓝图生成类的生命周期

```
1. 编辑器加载：从 .uasset 反序列化 UBlueprint
2. 编译请求：用户点击 Compile 或自动编译触发
3. 骨架类生成：SkeletonGeneratedClass（用于编辑时类型检查）
4. 完整编译：
   - CompileClassLayout() → 创建 UBlueprintGeneratedClass
   - CompileFunctions() → 生成 UFunction + 字节码
   - GenerateCodeFromClass() → FKismetCompilerVMBackend 输出 Script
5. 运行时加载：与原生 UClass 完全一致，通过 ProcessEvent() 执行
```

---

## Logic 层：调用链路与关键算法

### 调用链路 1：NewObject → 反射构造

```
NewObject<T>()
  └── StaticConstructObject_Internal()
        ├── 查找或加载 UClass
        ├── 通过 UClass::ClassConstructor 调用 C++ 构造函数
        │     └── AMyActor::AMyActor(const FObjectInitializer&)
        └── PostConstructInit（初始化 UProperty 默认值）
              └── UClass::PropertyLink 遍历
                    └── FProperty::CopyCompleteValue（从 CDO 拷贝默认值）
```

### 调用链路 2：蓝图调用 C++ 函数

```
蓝图节点：调用 "Take Damage"
  └── VM 执行 EX_Context + EX_FinalFunction
        └── UFunction::Invoke()
              ├── 如果 FUNC_Native：
              │     └── Func(Context, Stack, RESULT_PARAM)
              │           └── AMyActor::execTakeDamage()
              │                 └── 实际 C++ 函数体
              └── 如果非 Native（蓝图实现）：
                    └── ProcessLocalScriptFunction()
                          └── while(Step()) 解释执行
```

### 调用链路 3：属性序列化

```
FArchive& Ar << Object
  └── UClass::Serialize()
        └── 遍历 PropertyLink 链
              └── FProperty::SerializeItem()
                    ├── 基本类型：直接读写二进制
                    ├── UObject*：写入 FObjectPtr / SoftObjectPath
                    ├── Struct：递归 Serialize
                    └── Array/Map/Set：先写长度，再写元素
```

### 关键算法：PropertyLink 链的建立

```cpp
// Class.cpp 中的 Link() 函数
void UStruct::Link(FArchive& Ar, bool bRelinkExistingProperties)
{
    // 1. 按偏移排序所有属性（最派生 → 基类）
    // 2. 建立 PropertyLink 链（所有属性）
    // 3. 建立 RefLink 链（只包含 UObject* / FStruct* 等引用类型）
    // 4. 建立 DestructorLink 链（需要析构的非 POD 类型）
    // 5. 建立 PostConstructLink 链（需要后构造初始化的属性）
    
    for (FProperty* Property = ChildProperties; Property; Property = Property->Next)
    {
        Property->Link(Ar);
        
        // 加入对应链表
        if (Property->ContainsObjectReference())
            *RefLinkPtr = Property;
        if (Property->ContainsNon POD())
            *DestructorLinkPtr = Property;
        // ...
    }
}
```

---

## 可迁移到自研引擎的工程原理

### 1. 宏驱动的代码生成（Macro-Driven Codegen）

UE 用 `UCLASS()`/`UPROPERTY()` 等宏标记反射意图，由外部工具生成辅助代码。这种模式的核心优势：

- **零运行时开销**：反射信息在编译期生成，不依赖 RTTI
- **精确控制**：开发者明确标记哪些字段/函数需要反射
- **增量构建**：只有修改的头文件触发 UHT 重新解析

> **自研引擎迁移建议**：如果不需要完整蓝图支持，可以简化为仅生成序列化元数据；使用 Clang LibTooling 替代手写解析器，大幅降低维护成本。

### 2. 延迟注册解决初始化顺序问题

UE 的 `DeferredRegister` + `UObjectProcessRegistrants` 模式是典型的**两阶段初始化**：

```
阶段一（全局构造）：仅注册到队列，不做任何依赖操作
阶段二（系统初始化）：按依赖拓扑排序，逐个完成真正初始化
```

> **自研引擎迁移建议**：任何拥有跨模块/跨编译单元依赖的全局注册系统，都应该采用两阶段初始化，避免静态初始化顺序问题（Static Initialization Order Fiasco）。

### 3. 轻量级属性体系（FField）

UE 从 `UProperty`（UObject）迁移到 `FProperty`（FField）的核心动机：

- UObject 每个对象 ~120 bytes 开销（ flags、index、name、outer、class 等）
- 一个类可能有 50+ 属性，旧体系光是属性对象就占用 6KB+
- FField 体系只需 ~40 bytes/属性，且不参与 GC 遍历

> **自研引擎迁移建议**：如果引擎需要大量反射属性（如 ECS 的组件字段），应将元数据与对象生命周期分离，避免每个属性都承担 UObject 的完整开销。

### 4. 字节码 VM 的设计权衡

UE 蓝图 VM 的几个关键设计决策：

| 设计 | 优点 | 缺点 |
|------|------|------|
| 解释执行（非 JIT） | 跨平台一致、可调试、安全 | 性能较低（~10-100x 慢于 Native） |
| 基于栈的指令集 | 实现简单、指令紧凑 | 频繁内存访问 |
| 与 C++ 共享 UFunction | 统一调用接口 | 需要 thunk 转换 |
| Ubergraph 合并 | 减少函数调用开销 | 增加单函数复杂度 |

> **自研引擎迁移建议**：如果蓝图/脚本性能是瓶颈，可以考虑：
> - 将热点蓝图节点编译为 C++（类似 UE 的 Nativization）
> - 使用 WASM 或 LuaJIT 作为脚本后端，保留蓝图编辑器但替换 VM
> - 引入 AOT 编译（蓝图 → C++ → 机器码）

### 5. 编译器-VM 的严格契约

UE 的 `Script.h` 中明确定义了编译器和 VM 之间的字节码格式契约：

```cpp
// 编译器和 VM 必须同步修改的 typedef
typedef uint16 VariableSizeType;
typedef uint32 CodeSkipSizeType;  // 受 SCRIPT_LIMIT_BYTECODE_TO_64KB 控制

// 枚举值一旦确定就不能更改（向后兼容）
enum EExprToken : uint8 { ... };
```

> **自研引擎迁移建议**：如果设计自己的脚本系统，应将字节码格式版本化，并建立严格的兼容性检查机制，避免旧资源在新引擎中崩溃。

---

## 相关阅读

- [[UE-构建系统-源码解析：UHT 反射代码生成]] — UHT 的详细解析（第一阶段）
- [[UE-基础层-源码解析：UObject 系统]] — UObject 生命周期与 GC（第二阶段）
- [[UE-基础层-源码解析：CoreUObject 类型系统]] — UClass/UProperty 的详细设计（第二阶段）
- [[UE-专题：引擎整体骨架与系统组合]] — 反射系统在引擎启动中的位置（第八阶段）
- [[UE-专题：从代码到屏幕的编辑器工作流]] — 蓝图编译在编辑器工作流中的角色（第八阶段）

---

## 源码速查表

| 功能 | 文件路径 | 关键行号 |
|------|---------|---------|
| UHT 入口 | `Programs/Shared/EpicGames.UHT/UhtMain.cs` | - |
| UHT 类解析 | `Programs/Shared/EpicGames.UHT/Parsers/UhtClassParser.cs` | 第 43 行起 |
| UHT 代码生成 | `Programs/Shared/EpicGames.UHT/Exporters/CodeGen/UhtHeaderCodeGeneratorCppFile.cs` | 第 465 行起 |
| 宏定义 | `Runtime/CoreUObject/Public/UObject/ObjectMacros.h` | 第 755–770 行 |
| UField | `Runtime/CoreUObject/Public/UObject/Class.h` | 第 180 行起 |
| UStruct | `Runtime/CoreUObject/Public/UObject/Class.h` | 第 476 行起 |
| UClass | `Runtime/CoreUObject/Public/UObject/Class.h` | 第 3792 行起 |
| UFunction | `Runtime/CoreUObject/Public/UObject/Class.h` | 第 2475 行起 |
| FProperty | `Runtime/CoreUObject/Public/UObject/UnrealTypePrivate.h` | 第 17 行起 |
| 延迟注册 | `Runtime/CoreUObject/Private/UObject/UObjectBase.cpp` | 第 614 行起 |
| 初始化静态类 | `Runtime/CoreUObject/Private/UObject/Class.cpp` | 第 127 行起 |
| 蓝图编译入口 | `Editor/KismetCompiler/Private/KismetCompiler.cpp` | 第 5436 行起 |
| 类布局编译 | `Editor/KismetCompiler/Private/KismetCompiler.cpp` | 第 4732 行起 |
| VM 后端 | `Editor/KismetCompiler/Private/KismetCompilerVMBackend.cpp` | 第 2297 行起 |
| 中间表示 | `Editor/KismetCompiler/Public/BlueprintCompiledStatement.h` | 第 10 行起 |
| VM 执行入口 | `Runtime/CoreUObject/Private/UObject/ScriptCore.cpp` | 第 2015 行起 |
| VM 本地函数执行 | `Runtime/CoreUObject/Private/UObject/ScriptCore.cpp` | 第 1186 行起 |
| Step 分派 | `Runtime/CoreUObject/Private/UObject/ScriptCore.cpp` | 第 508 行起 |
| 字节码定义 | `Runtime/CoreUObject/Public/UObject/Script.h` | 第 189 行起 |
| 执行栈 | `Runtime/CoreUObject/Public/UObject/Stack.h` | 第 113 行起 |

---

> **维护记录**
> - 2026-04-19：完成第八阶段 [[UE-专题：反射与代码生成]] 的完整三层分析（UHT 代码生成链路、反射注册启动链路、蓝图编译器中间表示、蓝图 VM 字节码执行、可迁移工程原理）并更新索引状态。

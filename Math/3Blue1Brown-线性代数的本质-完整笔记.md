# 3Blue1Brown《线性代数的本质》完整笔记

> **参考视频**: [Essence of Linear Algebra - YouTube Playlist](https://www.youtube.com/playlist?list=PLZHQObOWTQDPD3MizzM2xVFitgF8hE_ab)  
> **B站中文**: [官方双语/合集]线性代数的本质 - 系列合集  
> **笔记来源**: 整合 [47saikyo](https://47saikyo.moe/) 和多方学习笔记

---

## 引言

> "尽管一批教授和教科书编者用关于矩阵的荒唐至极的计算内容掩盖了线性代数的简明性，但是鲜有与之相较更为初等的理论。" —— 让·迪厄多内

国内的线性代数教材往往过分专注于其**数值计算**的部分而对其几何含义缺少解释，而线性代数有许多被教材所忽视的、可视化的直观理解。当真正理解了几何直观与数值计算的关系时，线性代数的细节和其在各种领域的应用就会显得合情合理。

更重要的是，关于数值计算的部分，现代已经拥有了计算机来帮我们处理这部分问题，在实践中，我们更应当去关注概念层面的东西。

---

## 目录

1. [向量究竟是什么？](#一向量究竟是什么)
2. [线性组合、张成的空间与基](#二线性组合张成的空间与基)
3. [矩阵与线性变换](#三矩阵与线性变换)
4. [矩阵乘法与线性变换复合](#四矩阵乘法与线性变换复合)
5. [行列式](#五行列式)
6. [逆矩阵、列空间与零空间](#六逆矩阵列空间与零空间)
7. [点积与对偶性](#七点积与对偶性)
8. [叉积](#八叉积)
9. [基变换](#九基变换)
10. [特征值与特征向量](#十特征值与特征向量)
11. [抽象向量空间](#十一抽象向量空间)

---

## 一、向量究竟是什么？

### 1.1 三种看待向量的视角

| 专业视角 | 定义 | 特点 |
|---------|------|------|
| **物理专业** | 空间中的箭头，由方向和长度所定义 | 向量可在空间中任意移动而不改变 |
| **计算机专业** | 有序数列（列表） | 向量是数据的组织形式 |
| **数学专业** | 任何东西——只要能保证向量相加和数乘有意义 | 最抽象的定义 |

### 1.2 向量的表示

和物理专业的看法有一定出入的是，**线性代数中的向量往往以坐标原点起始**。当向量以坐标原点起始时，就可以通过一个有序列表来表示向量的坐标。

向量的坐标由一对数构成，描述了如何从向量的起点（原点）出发达到向量的终点：

$$
\vec{v} = \begin{bmatrix} x \\ y \end{bmatrix}
$$

> 注意：向量坐标通常竖着写，以和点坐标区分开。

### 1.3 向量加法

![向量加法示意图](https://upload.wikimedia.org/wikipedia/commons/thumb/5/52/Vector_addition.svg/400px-Vector_addition.svg.png)

**几何理解**: 将 $\vec{v}$ 平移，使其起点对准 $\vec{w}$ 的终点，最终画一条从 $\vec{v}$ 起点指向 $\vec{w}$ 终点的向量。

**代数计算**:
$$
\begin{bmatrix} x_1 \\ y_1 \end{bmatrix} + \begin{bmatrix} x_2 \\ y_2 \end{bmatrix} = \begin{bmatrix} x_1 + x_2 \\ y_1 + y_2 \end{bmatrix}
$$

**本质**: 向量加法代表了**移动的累加**——先沿着 $\vec{v}$ 移动，再从终点沿着 $\vec{w}$ 移动，等价于沿着 $(\vec{v} + \vec{w})$ 移动。

### 1.4 向量数乘（缩放）

![向量数乘](https://upload.wikimedia.org/wikipedia/commons/thumb/1/11/Scalar_multiplication.svg/400px-Scalar_multiplication.svg.png)

向量数乘即将原向量延长/缩短为原来的 $n$ 倍：

$$
c \cdot \begin{bmatrix} x \\ y \end{bmatrix} = \begin{bmatrix} cx \\ cy \end{bmatrix}
$$

- 当 $n > 1$ 时：向量伸长
- 当 $0 < n < 1$ 时：向量缩短
- 当 $n < 0$ 时：向量方向反转

这类运算被称为向量的**缩放**（Scaling），其中的数字被称作**标量**（Scalar）。

> 🔑 **核心洞察**: 向量加法和向量数乘贯穿线性代数的始终！

---

## 二、线性组合、张成的空间与基

### 2.1 基向量

在二维坐标系中有两个特殊含义的向量：

- **$\hat{i}$ (i-hat)**: x轴上的单位向量 $\begin{bmatrix} 1 \\ 0 \end{bmatrix}$
- **$\hat{j}$ (j-hat)**: y轴上的单位向量 $\begin{bmatrix} 0 \\ 1 \end{bmatrix}$

这些单位向量被叫做**坐标系的基向量**。

### 2.2 向量的线性组合

将向量的坐标看作**一组标量**，分别描述了该向量是 $\hat{i}$ 和 $\hat{j}$ 分别经过何种缩放后，再进行相加得到的：

$$
\begin{bmatrix} 3 \\ 2 \end{bmatrix} = 3\hat{i} + 2\hat{j} = 3\begin{bmatrix} 1 \\ 0 \end{bmatrix} + 2\begin{bmatrix} 0 \\ 1 \end{bmatrix}
$$

两个数乘向量的和被称作这两个向量的**线性组合**：

$$
a\vec{v} + b\vec{w}
$$

**重要观察**: 当我们用坐标描述一个向量时，它依赖于我们正在使用的**基向量**。不同的基向量会导致不同的结果。

### 2.3 张成的空间（Span）

![张成空间](https://upload.wikimedia.org/wikipedia/commons/thumb/2/26/Span.svg/500px-Span.svg.png)

对于一对初始向量的线性组合，如果我们让两个标量都自由变化：

- **大部分情况**: 可以得到平面中的任何一个向量 → 张成整个二维空间
- **两向量平行**: 得到的向量被限制在一条直线上
- **两向量都是零向量**: 只能得到原点

**定义**: 所有可以由 $a\vec{v} + b\vec{w}$ 得到的向量的集合，被称为向量 $\vec{v}$ 和 $\vec{w}$ **张成的空间**（Span）。

**三维扩展**: 
- 三维空间中两个向量张成的空间是这两个向量构成的平面
- 加上第三个不共面的向量，就能张成整个三维空间

### 2.4 线性相关与线性无关

![线性相关与无关](https://miro.medium.com/v2/resize:fit:640/format:webp/1*7eL5bI0Q6yX8z9Z8Y8Y8zQ.png)

**线性相关**（Linearly Dependent）:
在向量的线性组合中添加了一个向量，但是没有扩展张成的空间。此时存在：

$$
\vec{u} = a\vec{v} + b\vec{w}
$$

几何意义：至少有一个向量"多余"，可以表示为其他向量的线性组合。

**线性无关**（Linearly Independent）:
每个向量都给张成空间增加了新维度：

$$
a\vec{v} + b\vec{w} + c\vec{u} = \vec{0} \text{ 仅当 } a=b=c=0 \text{ 时成立}
$$

### 2.5 向量空间的基

**严格定义**: 张成该空间的一个**线性无关向量**的集合。

---

## 三、矩阵与线性变换

### 3.1 线性变换的本质

**线性变换**（Linear Transformation）：接受一个向量并输出一个向量的变换。

**线性的条件**:
1. 空间中的所有**直线**在变换后仍然是直线
2. **原点**的位置保持不变

**网格线特征**: 线性变换要保证**网格线平行且等距分布**。

### 3.2 用矩阵描述线性变换

![线性变换](https://upload.wikimedia.org/wikipedia/commons/thumb/a/a6/Linear_transformation.svg/500px-Linear_transformations.svg.png)

**关键洞察**: 
- 变换前：$\vec{v} = x\hat{i} + y\hat{j}$
- 变换后：$\vec{v}_{new} = x\hat{i}_{new} + y\hat{j}_{new}$

只要找到变换后的基向量，就能推出任意向量在变换后的位置！

**矩阵的本质**: 
> 矩阵是**对空间操纵的描述**，是一种线性空间变换的描述函数。

$$
M = \begin{bmatrix} a & b \\ c & d \end{bmatrix}
$$

- **第一列** $\begin{bmatrix} a \\ c \end{bmatrix}$: 变换后的 $\hat{i}$
- **第二列** $\begin{bmatrix} b \\ d \end{bmatrix}$: 变换后的 $\hat{j}$

### 3.3 矩阵与向量相乘

$$
\begin{bmatrix} a & b \\ c & d \end{bmatrix} \begin{bmatrix} x \\ y \end{bmatrix} = x\begin{bmatrix} a \\ c \end{bmatrix} + y\begin{bmatrix} b \\ d \end{bmatrix} = \begin{bmatrix} ax + by \\ cx + dy \end{bmatrix}
$$

**示例**: 逆时针旋转90°

$$
R_{90°} = \begin{bmatrix} 0 & -1 \\ 1 & 0 \end{bmatrix}
$$

验证：
- $\hat{i} = \begin{bmatrix} 1 \\ 0 \end{bmatrix}$ 变换后 → $\begin{bmatrix} 0 \\ 1 \end{bmatrix}$ ✓
- $\hat{j} = \begin{bmatrix} 0 \\ 1 \end{bmatrix}$ 变换后 → $\begin{bmatrix} -1 \\ 0 \end{bmatrix}$ ✓

---

## 四、矩阵乘法与线性变换复合

### 4.1 复合变换

![矩阵乘法](https://upload.wikimedia.org/wikipedia/commons/thumb/e/e5/Matrix_multiplication_diagram.svg/500px-Matrix_multiplication_diagram.svg.png)

我们可以对多个线性变换进行累加操作——先进行变换A，再在此基础上进行变换B，称之为**两个线性变换的复合变换**。

**读取顺序**: 矩阵乘法**从右往左**读！

$$
M_{total} \cdot \vec{v} = M_{剪切} \cdot (M_{旋转} \cdot \vec{v})
$$

先应用右侧的旋转矩阵，再应用左侧的剪切矩阵。

### 4.2 矩阵乘法公式

对于两个 $2×2$ 矩阵：

$$
\begin{bmatrix} a & b \\ c & d \end{bmatrix} \begin{bmatrix} e & f \\ g & h \end{bmatrix} = \begin{bmatrix} ae+bg & af+bh \\ ce+dg & cf+dh \end{bmatrix}
$$

**几何推导**: 
- 复合矩阵的**第一列** = 右侧矩阵第一列经过左侧变换后的结果
- 复合矩阵的**第二列** = 右侧矩阵第二列经过左侧变换后的结果

### 4.3 矩阵乘法的性质

| 性质 | 是否满足 | 说明 |
|------|---------|------|
| **交换律** | ❌ 不满足 | $AB \neq BA$（先旋转后剪切 ≠ 先剪切后旋转）|
| **结合律** | ✅ 满足 | $(AB)C = A(BC)$ |

**结合律的几何理解**: 两种公式都描述了"先进行C变换，再进行B变换，最后进行A变换"，因此没有区别。

---

## 五、行列式

### 5.1 行列式的几何意义

![行列式](https://upload.wikimedia.org/wikipedia/commons/thumb/a/a3/Determinant.svg/500px-Determinant.svg.png)

**核心定义**: 
> 行列式描述**线性变换改变面积的比例**。

$$
\det(M) = \text{变换后面积} / \text{变换前面积}
$$

**三维扩展**: 行列式是对**体积**变换比例的描述。

### 5.2 行列式的特殊情况

| 行列式值 | 几何意义 |
|---------|---------|
| $\det > 1$ | 空间被拉伸，面积放大 |
| $0 < \det < 1$ | 空间被压缩，面积缩小 |
| $\det = 0$ | 空间被压缩到更低维度（降维打击）|
| $\det < 0$ | 空间定向被翻转（如二维平面翻面）|

**降维检测**: 行列式为0意味着变换后的基向量是线性相关的，空间被压缩了。

### 5.3 行列式的计算

**二维矩阵**:

$$
\det\begin{bmatrix} a & b \\ c & d \end{bmatrix} = ad - bc
$$

**几何解释**:
- 当 $b=c=0$ 时，矩阵表示对x轴和y轴的独立缩放，面积变换倍率就是 $ad$
- $ad$ 代表主对角线拉伸后的面积
- $bc$ 代表副对角线的影响

**三维矩阵**:

$$
\det\begin{bmatrix} a & b & c \\ d & e & f \\ g & h & i \end{bmatrix} = a(ei-fh) - b(di-fg) + c(dh-eg)
$$

### 5.4 复合变换的行列式

$$
\det(M_1 M_2) = \det(M_1) \cdot \det(M_2)
$$

**几何证明**: 
- 先应用 $M_2$：面积变为原来的 $\det(M_2)$ 倍
- 再应用 $M_1$：面积再变为刚才的 $\det(M_1)$ 倍
- 总共：$\det(M_1) \cdot \det(M_2)$ 倍

---

## 六、逆矩阵、列空间与零空间

### 6.1 线性方程组的几何视角

线性方程组：

$$
\begin{cases} 2x + 5y = 8 \\ 3x + 4y = 7 \end{cases}
$$

可以写成矩阵形式：

$$
\begin{bmatrix} 2 & 5 \\ 3 & 4 \end{bmatrix} \begin{bmatrix} x \\ y \end{bmatrix} = \begin{bmatrix} 8 \\ 7 \end{bmatrix}
$$

**几何意义**: 
> 给定一个线性变换 $A$ 和一个向量 $\vec{v}$，找到一个向量 $\vec{x}$，使之经过该线性变换后与 $\vec{v}$ 重合。

### 6.2 逆矩阵

![逆矩阵](https://upload.wikimedia.org/wikipedia/commons/thumb/5/56/Invertible_Matrix.svg/500px-Invertible_Matrix.svg.png)

当 $\det(A) \neq 0$ 时，可以找到唯一的逆向变换 $A^{-1}$：

$$
A^{-1} A = I = \begin{bmatrix} 1 & 0 \\ 0 & 1 \end{bmatrix}
$$

其中 $I$ 是**恒等变换**（什么也不做）。

**求解方程**:

$$
A\vec{x} = \vec{v} \Rightarrow \vec{x} = A^{-1}\vec{v}
$$

**不可逆的情况** ($\det = 0$):
- 空间被压缩到更低维度（如二维→一维）
- 无法将一条直线"逆向"回一个平面
- 方程组可能无解或有无穷多解

### 6.3 秩（Rank）

**定义**: 线性变换后空间的维数。

- 若变换后空间变为二维 → 秩为2
- 若变换后空间变为一条直线 → 秩为1
- 若变换后空间变为原点 → 秩为0

**满秩**: 秩等于矩阵的列数。

### 6.4 列空间（Column Space）

**定义**: 矩阵的列向量所张成的空间，即线性变换所有可能的输出向量的集合。

$$
\text{Column Space}(A) = \text{span}\{\text{矩阵A的各列}\}
$$

**精确秩的定义**: 某个线性变换的**列空间的维数**。

### 6.5 零空间（Null Space / Kernel）

![零空间](https://upload.wikimedia.org/wikipedia/commons/thumb/5/53/Null_space.svg/500px-Null_space.svg.png)

**定义**: 变换后落在原点的所有向量的集合。

$$
\text{Null}(A) = \{\vec{x} \mid A\vec{x} = \vec{0}\}
$$

- **满秩矩阵**: 只有零向量落在原点（零空间只包含零向量）
- **非满秩矩阵**: 有一系列向量被压缩到原点（零空间有无穷多个向量）

---

## 七、点积与对偶性

### 7.1 点积的定义与计算

**代数定义**:

$$
\vec{a} \cdot \vec{b} = a_1b_1 + a_2b_2 + ... + a_nb_n
$$

**二维示例**:

$$
\begin{bmatrix} 1 \\ 2 \end{bmatrix} \cdot \begin{bmatrix} 3 \\ 4 \end{bmatrix} = 1×3 + 2×4 = 11
$$

### 7.2 点积的几何意义

![点积投影](https://upload.wikimedia.org/wikipedia/commons/thumb/6/6a/Inner_product.svg/500px-Inner_product.svg.png)

**核心公式**:

$$
\vec{a} \cdot \vec{b} = |\vec{a}| |\vec{b}| \cos\theta
$$

或者理解为：

$$
\vec{a} \cdot \vec{b} = (\vec{a} \text{在} \vec{b} \text{上的投影长度}) × |\vec{b}|
$$

**点积的符号含义**:
- $\vec{a} \cdot \vec{b} > 0$: 两向量方向大致相同（夹角 < 90°）
- $\vec{a} \cdot \vec{b} = 0$: 两向量垂直
- $\vec{a} \cdot \vec{b} < 0$: 两向量方向大致相反（夹角 > 90°）

### 7.3 对偶性（Duality）

> **对偶性**: 两种数学事物中自然而又出乎意料的对应关系。

**核心洞察**:

每当看到一个从**高维空间到一维空间**的线性变换，你都能找到一个向量（称为这个变换的**对偶向量**），使得：

$$
\text{应用线性变换} \Leftrightarrow \text{与对偶向量做点积}
$$

**几何推导**:

考虑将二维空间投影到一条过原点的直线上：

1. 设直线上的单位向量为 $\hat{u}$
2. 投影变换的矩阵可以表示为：
   $$
   \begin{bmatrix} u_x & u_y \end{bmatrix}
   $$
3. 这正是向量 $\hat{u}$ "躺下"的样子！

**结论**: 点积可以理解为**将其中一个向量转化为线性变换**，然后应用到另一个向量上。

---

## 八、叉积

### 8.1 叉积的基本属性

对于 $\vec{v} × \vec{w}$：

1. **数值**: 等于两个向量围成的平行四边形的面积
2. **方向**: 垂直于该平行四边形（右手定则）
3. **符号**: 当 $\vec{v}$ 在 $\vec{w}$ 右侧时数值为正

### 8.2 二维叉积（伪叉积）

实际上二维叉积的结果是一个**标量**（面积）：

$$
\vec{v} × \vec{w} = \det\begin{bmatrix} v_x & w_x \\ v_y & w_y \end{bmatrix} = v_x w_y - v_y w_x
$$

这等于两个向量张成的平行四边形的**有向面积**。

### 8.3 三维叉积

![叉积](https://upload.wikimedia.org/wikipedia/commons/thumb/4/4e/Cross_product.svg/500px-Cross_product.svg.png)

**定义**: 叉积是通过两个三维向量生成一个新三维向量的过程。

**计算公式**:

$$
\vec{v} × \vec{w} = \begin{bmatrix} v_y w_z - v_z w_y \\ v_z w_x - v_x w_z \\ v_x w_y - v_y w_x \end{bmatrix}
$$

**行列式记忆法**:

$$
\vec{v} × \vec{w} = \det\begin{bmatrix} \hat{i} & \hat{j} & \hat{k} \\ v_x & v_y & v_z \\ w_x & w_y & w_z \end{bmatrix}
$$

**输出向量的性质**:
- **长度**: $|\vec{v} × \vec{w}| = |\vec{v}| |\vec{w}| \sin\theta$ = 平行四边形面积
- **方向**: 垂直于 $\vec{v}$ 和 $\vec{w}$ 所在的平面（右手定则）

**右手定则**: 
- 食指指向 $\vec{v}$ 的方向
- 中指指向 $\vec{w}$ 的方向
- 大拇指指向叉积的方向

### 8.4 叉积的对偶性（选读）

**核心思想**: 三维向量 $\vec{v}$ 和 $\vec{w}$ 的叉积，就是寻找一个向量 $\vec{p}$，使得：

$$
\vec{p} \cdot \vec{x} = \det\begin{bmatrix} x_x & v_x & w_x \\ x_y & v_y & w_y \\ x_z & v_z & w_z \end{bmatrix}
$$

**几何解释**: 
- 右侧是三个向量围成的平行六面体的**有向体积**
- 左侧是 $\vec{x}$ 投影到 $\vec{p}$ 上的长度 × $|\vec{p}|$
- 当 $|\vec{p}|$ = 平行四边形面积时，两者相等

---

## 九、基变换

### 9.1 坐标系的依赖

每当我们用数字描述向量时，它都**依赖于我们正在使用的基**。

**标准坐标系**:
- $\hat{i} = \begin{bmatrix} 1 \\ 0 \end{bmatrix}$
- $\hat{j} = \begin{bmatrix} 0 \\ 1 \end{bmatrix}$

**Jennifer的坐标系**（以不同的基向量）：
- $\vec{b}_1 = \begin{bmatrix} 2 \\ 1 \end{bmatrix}$ （她眼中的 $\hat{i}$）
- $\vec{b}_2 = \begin{bmatrix} -1 \\ 1 \end{bmatrix}$ （她眼中的 $\hat{j}$）

### 9.2 基变换矩阵

**问题**: Jennifer用她的坐标 $\begin{bmatrix} -1 \\ 2 \end{bmatrix}_J$ 描述一个向量，这在我们的坐标系中是什么？

**解答**:

$$
\begin{bmatrix} 2 & -1 \\ 1 & 1 \end{bmatrix} \begin{bmatrix} -1 \\ 2 \end{bmatrix} = (-1)\begin{bmatrix} 2 \\ 1 \end{bmatrix} + 2\begin{bmatrix} -1 \\ 1 \end{bmatrix} = \begin{bmatrix} -4 \\ 1 \end{bmatrix}
$$

**基变换矩阵**: 以新的基向量为列构成的矩阵。

### 9.3 视角转换的公式

| 转换方向 | 操作 |
|---------|------|
| Jennifer → 我们 | 乘以基变换矩阵 $A$ |
| 我们 → Jennifer | 乘以基变换矩阵的逆 $A^{-1}$ |

### 9.4 基变换后的线性变换

**场景**: 我们知道在我们坐标系中旋转90°的矩阵，如何求在Jennifer坐标系中这个变换的矩阵？

**步骤**:
1. 将Jennifer的向量转换到我们的坐标系: $\vec{x}_{ours} = A\vec{x}_{Jen}$
2. 应用旋转: $\vec{x}'_{ours} = M_{rotate} A\vec{x}_{Jen}$
3. 转回Jennifer的坐标系: $\vec{x}'_{Jen} = A^{-1} M_{rotate} A\vec{x}_{Jen}$

**公式**:

$$
M_{Jen} = A^{-1} M_{ours} A
$$

这被称为**相似变换**。

---

## 十、特征值与特征向量

### 10.1 定义

![特征向量](https://upload.wikimedia.org/wikipedia/commons/thumb/5/58/Eigenvectors.svg/500px-Eigenvectors.svg.png)

**特征向量**: 变换后仍留在其张成空间内的向量（即方向不变，只被拉伸/压缩）。

**特征值**: 衡量特征向量在变换中拉伸或压缩比例的因子。

**数学表达**:

$$
A\vec{v} = \lambda\vec{v}
$$

- $\vec{v}$: 特征向量
- $\lambda$: 特征值

### 10.2 几何示例

对于矩阵 $A = \begin{bmatrix} 2 & 0 \\ 0 & 3 \end{bmatrix}$：

- x轴上的向量: 特征值 = 2，被拉伸2倍
- y轴上的向量: 特征值 = 3，被拉伸3倍

**旋转变换**: 在三维空间中，旋转轴就是特征向量，特征值为1（长度不变）。

### 10.3 特征值与特征向量的计算

从 $A\vec{v} = \lambda\vec{v}$ 出发：

$$
A\vec{v} - \lambda\vec{v} = \vec{0} \Rightarrow (A - \lambda I)\vec{v} = \vec{0}
$$

**关键洞察**: 
- 当 $\det(A - \lambda I) = 0$ 时，存在非零解
- 这个条件用于求解特征值

**计算步骤**:

1. **特征方程**: $\det(A - \lambda I) = 0$
2. 解出特征值 $\lambda$
3. 对每个 $\lambda$，解 $(A - \lambda I)\vec{v} = \vec{0}$ 得到特征向量

**示例**:

对于 $A = \begin{bmatrix} 2 & 1 \\ 1 & 2 \end{bmatrix}$：

$$
\det\begin{bmatrix} 2-\lambda & 1 \\ 1 & 2-\lambda \end{bmatrix} = (2-\lambda)^2 - 1 = \lambda^2 - 4\lambda + 3 = 0
$$

解得：$\lambda_1 = 3, \lambda_2 = 1$

### 10.4 特征基与对角化

**对角矩阵**: 基向量都是特征向量，对角元是特征值。

$$
D = \begin{bmatrix} \lambda_1 & 0 \\ 0 & \lambda_2 \end{bmatrix}
$$

**对角化的威力**: 

$$
A^n = (PDP^{-1})^n = PD^nP^{-1}
$$

其中：
- $P$: 特征向量构成的矩阵
- $D$: 特征值构成的对角矩阵

**可对角化的条件**: 矩阵有足够多的线性无关特征向量（能张成全空间）。

---

## 十一、抽象向量空间

### 11.1 向量的本质

回到最开始的问题：什么是向量？

| 观点 | 描述 |
|------|------|
| **几何观点** | 空间中的箭头，用坐标表示 |
| **代数观点** | 一组数字，恰好能用箭头表示 |
| **抽象观点** | 任何满足向量公理的对象 |

### 11.2 函数作为向量

**观察**: 函数也满足向量的所有性质！

| 向量运算 | 函数对应 |
|---------|---------|
| 向量加法 $\vec{v} + \vec{w}$ | 函数加法 $f(x) + g(x)$ |
| 数乘 $c\vec{v}$ | 数乘 $c \cdot f(x)$ |
| 线性变换 | 线性算子（如求导）|

**求导是线性运算**:

$$
\frac{d}{dx}(f + g) = \frac{df}{dx} + \frac{dg}{dx}
$$

$$
\frac{d}{dx}(cf) = c\frac{df}{dx}
$$

### 11.3 向量空间的公理

任何集合 $V$ 如果满足以下8条公理，就是一个**向量空间**：

1. **加法交换律**: $\vec{u} + \vec{v} = \vec{v} + \vec{u}$
2. **加法结合律**: $(\vec{u} + \vec{v}) + \vec{w} = \vec{u} + (\vec{v} + \vec{w})$
3. **零向量存在**: $\vec{v} + \vec{0} = \vec{v}$
4. **加法逆元**: $\vec{v} + (-\vec{v}) = \vec{0}$
5. **数乘单位元**: $1\vec{v} = \vec{v}$
6. **数乘结合律**: $c(d\vec{v}) = (cd)\vec{v}$
7. **数乘分配律（对向量）**: $c(\vec{u} + \vec{v}) = c\vec{u} + c\vec{v}$
8. **数乘分配律（对标量）**: $(c + d)\vec{v} = c\vec{v} + d\vec{v}$

### 11.4 普适性的代价

> "普适的代价是抽象。"

- **数学家**: 只负责定义公理和推导结论（定义接口和提供服务）
- **应用者**: 负责实现接口并使用服务

线性代数的美妙之处在于：**它不关心向量本身是什么**，只关心向量如何相加和数乘。

---

## 附录：关键公式汇总

### 向量运算

```
向量加法:     [x1]   [x2]   [x1+x2]
             [y1] + [y2] = [y1+y2]

数乘:         c[x]   [cx]
               [y] = [cy]

点积:         [a]   [b]         
             [c] · [d] = ab + cd

叉积(2D):     det[vx wx] = vx*wy - vy*wx
               [vy wy]

叉积(3D):     [vx]   [wx]   [vy*wz - vz*wy]
             [vy] × [wy] = [vz*wx - vx*wz]
             [vz]   [wz]   [vx*wy - vy*wx]
```

### 矩阵运算

```
矩阵乘法:     [a b][e f]   [ae+bg af+bh]
             [c d][g h] = [ce+dg cf+dh]

行列式(2D):   det[a b] = ad - bc
                  [c d]

逆矩阵(2D):   [a b]-1    1   [ d -b]
             [c d]   = ---- × [-c  a]
                       ad-bc
```

### 特征值与特征向量

```
定义:         A·v = λ·v

特征方程:     det(A - λI) = 0

对角化:       A = PDP⁻¹
              其中 P = [特征向量], D = diag(特征值)
```

---

## 参考资源

- [3Blue1Brown 官方 YouTube 播放列表](https://www.youtube.com/playlist?list=PLZHQObOWTQDPD3MizzM2xVFitgF8hE_ab)
- [B站官方双语合集](https://www.bilibili.com/video/BV1ys411472E)
- [47saikyo 的笔记](https://47saikyo.moe/)
- [知乎专栏 - 3Blue1Brown线性代数的本质学习笔记](https://zhuanlan.zhihu.com/p/6493794974)

---

> 💡 **学习建议**: 
> 1. 先看原视频理解几何直观
> 2. 结合本笔记复习核心概念
> 3. 自己动手推导公式
> 4. 尝试用代码实现这些变换（如 Python + NumPy）

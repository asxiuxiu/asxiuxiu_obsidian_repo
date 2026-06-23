---
title: Python 与 NumPy 基础
date: 2026-06-23
tags:
  - deep-learning
  - python
  - numpy
aliases:
  - Python 与 NumPy 基础
---

> [[Notes/深度学习入门/Roadmap|← 返回 深度学习入门路线图]]

# Python 与 NumPy 基础

如果只用 Python 原生的列表和循环来实现神经网络，代码会又慢又啰嗦。NumPy 的作用就是让我们用接近数学表达的方式，同时高效地处理大量数字。这一章的目标很简单：**熟悉 NumPy 的数组和矩阵运算**，为后面的感知机和神经网络打好基础。

---

## 为什么要用 NumPy

神经网络里的计算，本质上是一堆矩阵和向量操作：输入向量乘权重矩阵、加偏置、再传给激活函数。用 Python 的 `for` 循环一行一行算当然可以，但速度慢，而且代码不像数学公式那么直观。

NumPy 提供了 **ndarray**（N-dimensional array，N 维数组）对象，让我们可以像写数学公式一样写代码，同时底层用 C 实现，计算速度也快得多。

```python
import numpy as np

# 创建一维数组（向量）
a = np.array([1.0, 2.0, 3.0])
print(a)          # [1. 2. 3.]
print(type(a))    # <class 'numpy.ndarray'>
```

---

## NumPy 数组的基本操作

ndarray 和 Python 列表有点像，但支持更方便的数学运算。

```python
import numpy as np

a = np.array([1.0, 2.0, 3.0])
b = np.array([2.0, 4.0, 6.0])

# 对应元素相加减乘除
print(a + b)  # [3. 6. 9.]
print(a - b)  # [-1. -2. -3.]
print(a * b)  # [2. 8. 18.]
print(a / b)  # [0.5 0.5 0.5]
```

注意这里的 `*` 是**对应元素相乘**（element-wise），不是矩阵乘法。矩阵乘法要用 `np.dot`。

ndarray 还能直接对整个数组做标量运算：

```python
a = np.array([1.0, 2.0, 3.0])
print(a / 2.0)  # [0.5 1.  1.5]
print(a * 2.0)  # [2. 4. 6.]
```

---

## 多维数组与形状

神经网络里最常见的是二维数组（矩阵）。

```python
import numpy as np

A = np.array([[1, 2], [3, 4], [5, 6]])
print(A)
# [[1 2]
#  [3 4]
#  [5 6]]

print(A.shape)  # (3, 2)：3 行 2 列
print(A.dtype)  # int64：元素类型
```

通过 `shape` 可以查看数组的维度，这对后面排查神经网络里的维度错误非常重要。

---

## 矩阵乘法：np.dot

矩阵乘法是神经网络前向传播的核心。如果有输入向量 $\mathbf{x}$ 和权重矩阵 $\mathbf{W}$，它们的乘积 $\mathbf{x} \mathbf{W}$ 就可以一次性算出所有神经元的加权输入。

```python
import numpy as np

A = np.array([[1, 2], [3, 4]])
B = np.array([[5, 6], [7, 8]])

print(np.dot(A, B))
# [[19 22]
#  [43 50]]
```

对于向量来说，`np.dot` 就是点积：

```python
a = np.array([1, 2, 3])
b = np.array([4, 5, 6])
print(np.dot(a, b))  # 32
```

在第 3 章实现神经网络时，我们会频繁用到：

```python
y = np.dot(x, W) + b
```

---

## 广播：让形状不同的数组也能运算

广播（broadcasting）是 NumPy 的一个强大机制：当两个数组形状不完全一致时，NumPy 会自动把它们扩展到可以运算的形状。

最常见的场景就是矩阵加向量：

```python
import numpy as np

A = np.array([[1, 2], [3, 4], [5, 6]])
b = np.array([10, 20])

print(A + b)
# [[11 22]
#  [13 24]
#  [15 26]]
```

这里 `b` 被自动复制成 3 行，分别加到 `A` 的每一行上。在神经网络里，偏置 $\mathbf{b}$ 通常就是用一个向量，通过广播加到每一批样本的加权输入上。

---

## 用 Matplotlib 画简单的图

训练神经网络时，我们常常需要把损失函数的变化画出来，看看模型是不是真的在学习。

```python
import numpy as np
import matplotlib.pyplot as plt

x = np.arange(0, 6, 0.1)
y = np.sin(x)

plt.plot(x, y)
plt.xlabel("x")
plt.ylabel("sin(x)")
plt.show()
```

`np.arange(0, 6, 0.1)` 生成从 0 到 6、步长 0.1 的等差数列；`plt.plot` 把 $x$ 和 $y$ 对应的点连成线。

---

## 向量化：阶跃函数与 Sigmoid

感知机里会用到**阶跃函数**（step function）：输入大于 0 输出 1，否则输出 0。神经网络里更常用的是 **sigmoid 函数**：

$$
\sigma(x) = \frac{1}{1 + e^{-x}}
$$

NumPy 的好处是，只要把公式直接写出来，它就能自动对整个数组做计算：

```python
import numpy as np
import matplotlib.pyplot as plt

x = np.arange(-5.0, 5.0, 0.1)
y = 1 / (1 + np.exp(-x))

plt.plot(x, y)
plt.ylim(-0.1, 1.1)
plt.show()
```

这里 `np.exp(-x)` 会对 `x` 里的每一个元素求 $e^{-x}$，最后 `y` 是一个和 `x` 形状相同的数组。这种写法叫做**向量化**（vectorization），它避免了写 `for` 循环，也更接近数学公式。

---

## 小结

- **NumPy ndarray** 是深度学习的核心数据结构，用来表示向量、矩阵和张量。
- `np.dot` 是矩阵乘法，和对应元素相乘 `*` 要区分开。
- **广播**能让形状不同的数组直接做加减，简化偏置等操作。
- **Matplotlib** 常用于画出损失曲线、激活函数图像等。
- 把数学公式写成**向量化**代码，是后面实现神经网络的基本功。

---

> [[Notes/深度学习入门/Roadmap|← 返回 深度学习入门路线图]]

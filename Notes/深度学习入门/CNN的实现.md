---
title: CNN 的实现
date: 2026-06-23
tags:
  - deep-learning
aliases:
  - CNN Implementation
---

> [[Notes/深度学习入门/Roadmap|← 返回 深度学习入门路线图]]

# CNN 的实现

上一篇笔记我们已经知道，卷积层和池化层是怎么工作的。但如果真要把它们写成一个可以训练的网络，就会遇到一个麻烦：卷积的滑动窗口运算里嵌了好几层循环，在 Python 里直接写会非常慢。有没有什么办法，既能保持卷积的数学意义，又能利用矩阵乘法的高效实现？答案就是 **im2col**：把图像的小块“展开”成矩阵的行，把滤波器展开成矩阵的列，卷积就变成了一次矩阵乘法。

---

## im2col：把局部区域变成矩阵行

im2col 是 “image to column” 的缩写。它的作用是把输入图像中每个滤波器覆盖的局部区域，拉成一行向量。假设输入是 $N \times C \times H \times W$，滤波器高和宽都是 $FH$、$FW$，步幅和填充已知，那么输出特征图的高和宽分别是 $OH$、$OW$。im2col 的输出形状就是：

$$
(N \cdot OH \cdot OW) \times (C \cdot FH \cdot FW)
$$

每一行对应一个输出位置上的局部区域，每一列对应一个输入通道在一个滤波器位置上的值。

为什么这样做？因为矩阵乘法在 NumPy 和 BLAS 里已经被优化得很好，把卷积改写成矩阵乘法后，速度能快几十倍甚至上百倍。

```python
import numpy as np


def im2col(input_data, filter_h, filter_w, stride=1, pad=0):
    """把 N x C x H x W 的四维输入展开成适合卷积的二维矩阵。"""
    N, C, H, W = input_data.shape
    OH = (H + 2 * pad - filter_h) // stride + 1
    OW = (W + 2 * pad - filter_w) // stride + 1

    img = np.pad(input_data, ((0, 0), (0, 0), (pad, pad), (pad, pad)),
                 mode='constant')
    col = np.zeros((N, C, filter_h, filter_w, OH, OW))

    for y in range(filter_h):
        y_max = y + stride * OH
        for x in range(filter_w):
            x_max = x + stride * OW
            col[:, :, y, x, :, :] = img[:, :, y:y_max:stride, x:x_max:stride]

    col = col.transpose(0, 4, 5, 1, 2, 3).reshape(N * OH * OW, -1)
    return col
```

im2col 会占用更多内存，因为同一个像素会出现在很多行里，但它换来的计算效率提升非常值得。

---

## col2im：把展开后的矩阵还原回图像

有了 forward 时的 im2col，backward 时就需要把梯度从二维矩阵还原回四维图像，这就是 col2im。它把每个局部区域的梯度叠加回原始输入的对应位置。因为 im2col 有重叠，所以还原时需要“累加”而不是简单赋值。

```python
def col2im(col, input_shape, filter_h, filter_w, stride=1, pad=0):
    """im2col 的逆操作：把二维矩阵还原回 N x C x H x W。"""
    N, C, H, W = input_shape
    OH = (H + 2 * pad - filter_h) // stride + 1
    OW = (W + 2 * pad - filter_w) // stride + 1
    col = col.reshape(N, OH, OW, C, filter_h, filter_w).transpose(
        0, 3, 4, 5, 1, 2)

    img = np.zeros((N, C, H + 2 * pad + stride - 1, W + 2 * pad + stride - 1))
    for y in range(filter_h):
        y_max = y + stride * OH
        for x in range(filter_w):
            x_max = x + stride * OW
            img[:, :, y:y_max:stride, x:x_max:stride] += col[:, :, y, x, :, :]

    return img[:, :, pad:H + pad, pad:W + pad]
```

---

## 卷积层：用 im2col 实现

把输入和滤波器都展开后，卷积层的前向传播就只剩两步：

1. 用 `im2col` 把输入展开成二维矩阵 `col`。
2. 把滤波器 `W` 展开成二维矩阵 `col_W`（形状是 $(FN, C \cdot FH \cdot FW)$，再转置）。
3. 做矩阵乘法 `col @ col_W + b`，再 reshape 回 $N \times FN \times OH \times OW$。

反向传播时：

- 对 `b` 的梯度是输出梯度的 batch 方向求和。
- 对 `W` 的梯度是 `col.T @ dout`，再 reshape 回滤波器形状。
- 对输入的梯度是 `dout @ col_W.T`，再用 `col2im` 还原。

```python
class Convolution:
    def __init__(self, W, b, stride=1, pad=0):
        self.W = W
        self.b = b
        self.stride = stride
        self.pad = pad
        self.x = None
        self.col = None
        self.col_W = None
        self.dW = None
        self.db = None

    def forward(self, x):
        FN, C, FH, FW = self.W.shape
        N, C, H, W = x.shape
        OH = (H + 2 * self.pad - FH) // self.stride + 1
        OW = (W + 2 * self.pad - FW) // self.stride + 1

        col = im2col(x, FH, FW, self.stride, self.pad)
        col_W = self.W.reshape(FN, -1).T

        out = np.dot(col, col_W) + self.b
        out = out.reshape(N, OH, OW, FN).transpose(0, 3, 1, 2)

        self.x = x
        self.col = col
        self.col_W = col_W

        return out

    def backward(self, dout):
        FN, C, FH, FW = self.W.shape
        dout = dout.transpose(0, 2, 3, 1).reshape(-1, FN)

        self.db = np.sum(dout, axis=0)
        self.dW = np.dot(self.col.T, dout)
        self.dW = self.dW.transpose(1, 0).reshape(FN, C, FH, FW)

        dcol = np.dot(dout, self.col_W.T)
        dx = col2im(dcol, self.x.shape, FH, FW, self.stride, self.pad)

        return dx
```

这里的关键是：forward 时把 `W` reshape 成 `(FN, C*FH*FW)` 再转置；backward 时要把 `dW` 重新 reshape 回 `(FN, C, FH, FW)`。

---

## 池化层：保留最大值的位置

最大池化的前向传播也可以用 im2col：把每个池化窗口拉成一行，然后取每行的最大值。反向传播时，只把梯度传给最大值所在的那个位置，其他位置都是 0。

为了做到这一点，前向传播时要记住每行的最大值索引 `arg_max`，反向时用它构造一个“独热”形式的梯度矩阵。

```python
class Pooling:
    def __init__(self, pool_h, pool_w, stride=2, pad=0):
        self.pool_h = pool_h
        self.pool_w = pool_w
        self.stride = stride
        self.pad = pad
        self.x = None
        self.arg_max = None

    def forward(self, x):
        N, C, H, W = x.shape
        OH = (H - self.pool_h) // self.stride + 1
        OW = (W - self.pool_w) // self.stride + 1

        col = im2col(x, self.pool_h, self.pool_w, self.stride, self.pad)
        col = col.reshape(-1, self.pool_h * self.pool_w)

        arg_max = np.argmax(col, axis=1)
        out = np.max(col, axis=1)
        out = out.reshape(N, OH, OW, C).transpose(0, 3, 1, 2)

        self.x = x
        self.arg_max = arg_max

        return out

    def backward(self, dout):
        dout = dout.transpose(0, 2, 3, 1)

        pool_size = self.pool_h * self.pool_w
        dmax = np.zeros((dout.size, pool_size))
        dmax[np.arange(self.arg_max.size), self.arg_max.flatten()] = \
            dout.flatten()
        dmax = dmax.reshape(dout.shape + (pool_size,))

        dcol = dmax.reshape(dmax.shape[0] * dmax.shape[1] * dmax.shape[2], -1)
        dx = col2im(dcol, self.x.shape, self.pool_h, self.pool_w,
                    self.stride, self.pad)

        return dx
```

---

## SimpleConvNet：把卷积、池化、全连接组装起来

现在我们可以像搭积木一样组装一个最简单的 CNN：

```
输入图像
  → Convolution
  → ReLU
  → Pooling
  → Flatten（展平）
  → Affine
  → ReLU
  → Affine
  → Softmax with Loss
```

其中 `Flatten` 层只做一件事：把池化后的四维输出 `(N, C, H, W)` 展成二维 `(N, C*H*W)`，这样后面才能接全连接层。

```python
class Flatten:
    def __init__(self):
        self.input_shape = None

    def forward(self, x):
        self.input_shape = x.shape
        return x.reshape(x.shape[0], -1)

    def backward(self, dout):
        return dout.reshape(self.input_shape)
```

`SimpleConvNet` 的初始化需要根据输入尺寸和卷积参数，自动算出展平后的大小，从而确定全连接层 `W2` 的输入维度。

```python
class SimpleConvNet:
    def __init__(self, input_dim=(1, 28, 28),
                 conv_param={'filter_num': 30, 'filter_size': 5,
                             'pad': 0, 'stride': 1},
                 hidden_size=100, output_size=10, weight_init_std=0.01):
        filter_num = conv_param['filter_num']
        filter_size = conv_param['filter_size']
        filter_pad = conv_param['pad']
        filter_stride = conv_param['stride']
        input_size = input_dim[1]

        conv_output_size = (input_size - filter_size + 2 * filter_pad) // \
            filter_stride + 1
        pool_output_size = int(filter_num * (conv_output_size / 2) ** 2)

        self.params = {}
        self.params['W1'] = weight_init_std * np.random.randn(
            filter_num, input_dim[0], filter_size, filter_size)
        self.params['b1'] = np.zeros(filter_num)
        self.params['W2'] = weight_init_std * np.random.randn(
            pool_output_size, hidden_size)
        self.params['b2'] = np.zeros(hidden_size)
        self.params['W3'] = weight_init_std * np.random.randn(
            hidden_size, output_size)
        self.params['b3'] = np.zeros(output_size)

        self.layers = {}
        self.layers['Conv1'] = Convolution(self.params['W1'],
                                           self.params['b1'],
                                           filter_stride, filter_pad)
        self.layers['Relu1'] = Relu()
        self.layers['Pool1'] = Pooling(pool_h=2, pool_w=2, stride=2)
        self.layers['Flatten1'] = Flatten()
        self.layers['Affine1'] = Affine(self.params['W2'],
                                        self.params['b2'])
        self.layers['Relu2'] = Relu()
        self.layers['Affine2'] = Affine(self.params['W3'],
                                        self.params['b3'])
        self.last_layer = SoftmaxWithLoss()

    def predict(self, x):
        for layer in self.layers.values():
            x = layer.forward(x)
        return x

    def loss(self, x, t):
        y = self.predict(x)
        return self.last_layer.forward(y, t)

    def gradient(self, x, t):
        self.loss(x, t)

        dout = 1
        dout = self.last_layer.backward(dout)

        layers = list(self.layers.values())
        layers.reverse()
        for layer in layers:
            dout = layer.backward(dout)

        grads = {}
        grads['W1'] = self.layers['Conv1'].dW
        grads['b1'] = self.layers['Conv1'].db
        grads['W2'] = self.layers['Affine1'].dW
        grads['b2'] = self.layers['Affine1'].db
        grads['W3'] = self.layers['Affine2'].dW
        grads['b3'] = self.layers['Affine2'].db

        return grads
```

这里 `Relu`、`Affine`、`SoftmaxWithLoss` 都是之前反向传播笔记里已经实现过的层，可以直接复用。

---

## 训练循环

训练逻辑和之前几乎一样：每次取一个 mini-batch，计算梯度，更新参数。下面这段代码展示了如何构造网络并训练几步（实际训练 MNIST 时，只需把随机数据换成真实数据）：

```python
net = SimpleConvNet(input_dim=(1, 28, 28),
                    conv_param={'filter_num': 30, 'filter_size': 5,
                                'pad': 0, 'stride': 1},
                    hidden_size=100, output_size=10)

# 假设 x_batch, t_batch 是 mini-batch 数据
# x_batch 形状：(N, 1, 28, 28)
# t_batch 形状：(N, 10) 或 (N,)
for i in range(10000):
    grads = net.gradient(x_batch, t_batch)
    for key in ('W1', 'b1', 'W2', 'b2', 'W3', 'b3'):
        net.params[key] -= 0.01 * grads[key]
```

通常会把优化器封装成类，比如 [[Notes/深度学习入门/学习技巧|学习技巧]] 里介绍的 SGD、Adam，这样训练代码会更清晰。

---

## 可视化第一层权重

CNN 训练完成后，第一层卷积的滤波器往往很有趣。如果输入是 MNIST 这样的灰度图，第一层滤波器通常会长得像边缘、纹理等低层特征。

```python
import matplotlib.pyplot as plt


def filter_show(filters, nx=8, margin=3, scale=10):
    FN, C, FH, FW = filters.shape
    ny = int(np.ceil(FN / nx))

    fig = plt.figure()
    fig.subplots_adjust(left=0, right=1, bottom=0, top=1,
                        hspace=0.05, wspace=0.05)

    for i in range(FN):
        ax = fig.add_subplot(ny, nx, i + 1, xticks=[], yticks=[])
        ax.imshow(filters[i, 0], cmap=plt.cm.gray_r,
                  interpolation='nearest')
    plt.show()


filter_show(net.params['W1'], nx=8)
```

如果训练充分，你会看到一些滤波器对竖线敏感，一些对横线敏感，还有一些对斜边敏感。这说明网络自己学会了“看”图像的基本方式。

---

## 小结

- **im2col** 把卷积的局部窗口运算转换成矩阵乘法，大幅提升实现效率。
- **col2im** 负责反向传播时把梯度还原回原始图像形状。
- **Convolution 层** 的核心就是一次 `col @ col_W + b`，反向时分别求出 `dW`、`db`、`dx`。
- **Pooling 层** 通过记录前向时的最大值位置，反向时只把梯度回传给那个位置。
- 一个完整 CNN 需要把卷积、ReLU、池化、展平、全连接按顺序组装，和全连接网络共享同一套反向传播框架。
- 第一层卷积滤波器的可视化，是理解 CNN“看到了什么”的最直观方式。

---

**相关笔记**：

- [[Notes/深度学习入门/卷积层与池化层|卷积层与池化层]]
- [[Notes/深度学习入门/反向传播的实现|反向传播的实现]]
- [[Notes/深度学习入门/学习技巧|学习技巧]]
- [[Notes/深度学习入门/3层神经网络的实现|3 层神经网络的实现]]

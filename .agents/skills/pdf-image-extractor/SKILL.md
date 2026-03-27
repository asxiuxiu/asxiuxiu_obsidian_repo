# PDF 插图提取与裁剪 Skill

从 PDF 文档中提取、渲染和裁剪概念图与示意图。适用于从课程讲义、技术文档中提取有价值的可视化内容。

## 快速开始

```python
from pdf_toolkit import PDFImageToolkit, batch_crop_concepts

# 方法1: 使用工具类（推荐）
with PDFImageToolkit("lecture.pdf") as toolkit:
    # 渲染第12页（1-based页码转0-based）
    toolkit.render_page(11, zoom=2.0, output_path="page_12.png")
    
    # 裁剪页面右侧区域 (x0, y0, x1, y1) 使用比例坐标 0.0-1.0
    toolkit.crop_page(11, (0.50, 0.15, 0.98, 0.95), output_path="diagram.png")

# 方法2: 批量裁剪
concepts = {
    "rendering_pipeline": {"page": 11, "box": (0.50, 0.15, 0.98, 0.95)},
    "materials": {"page": 30, "box": (0.10, 0.25, 0.90, 0.75)},
}
batch_crop_concepts("lecture.pdf", "./output", concepts)
```

## 安装依赖

```bash
pip install PyMuPDF
```

## 工作流程

### 1. 分析 PDF 结构

```python
with PDFImageToolkit("lecture.pdf") as toolkit:
    # 均匀采样10页，查看内容和图片分布
    infos = toolkit.analyze_content(sample_pages=10)
    # 输出: Page 12: 236 chars, 7 images (文字少、图片多 = 可能是概念图页面)
```

### 2. 渲染关键页面预览

```python
# 渲染特定页码用于预览（页码从0开始）
toolkit.render_pages([11, 30, 45], output_dir="./preview", zoom=2.0)
```

### 3. 精确裁剪概念图

```python
# 常用裁剪区域（针对PPT式PDF）：
# - 右侧图表: (0.50, 0.15, 0.98, 0.95)
# - 左侧代码: (0.05, 0.15, 0.45, 0.95)  
# - 全页内容: (0.05, 0.15, 0.95, 0.95)
# - 中间图示: (0.10, 0.25, 0.90, 0.75)

toolkit.crop_page(
    page_num=11,           # 第12页（0-based）
    crop_box=(0.50, 0.15, 0.98, 0.95),  # 右侧区域
    zoom=2.0,              # 2x缩放保证清晰度
    output_path="concept.png"
)
```

### 4. 提取内嵌图片

```python
# 提取PDF中嵌入的图片资源（不同于渲染页面）
paths = toolkit.extract_embedded_images(11, output_dir="./embedded")
```

## 坐标系统说明

裁剪使用**比例坐标** (0.0-1.0)，相对于页面宽高：

```
(0.0, 0.0) ----------> (1.0, 0.0)
    |                      |
    |    页面内容区域       |
    |                      |
(0.0, 1.0) ----------> (1.0, 1.0)
```

常见布局的裁剪配置：

| 布局类型 | crop_box | 说明 |
|---------|----------|------|
| 右侧图表 | `(0.50, 0.15, 0.98, 0.95)` | PPT右侧的概念图 |
| 左侧文字 | `(0.05, 0.15, 0.45, 0.95)` | PPT左侧的文字说明 |
| 全宽内容 | `(0.05, 0.18, 0.95, 0.88)` | 占满宽度的图示 |
| 中间聚焦 | `(0.10, 0.25, 0.90, 0.75)` | 去掉标题和页脚的图表 |

## 命令行使用

```bash
# 渲染页面
python pdf_toolkit.py render "lecture.pdf" 12

# 裁剪区域（比例坐标）
python pdf_toolkit.py crop "lecture.pdf" 12 0.5 0.15 0.98 0.95 diagram.png

# 分析内容
python pdf_toolkit.py analyze "lecture.pdf" 15

# 提取内嵌图片
python pdf_toolkit.py extract "lecture.pdf" 12 ./images
```

## 完整示例：提取 GAMES104 插图

```python
from pdf_toolkit import PDFImageToolkit, batch_crop_concepts

# 定义需要裁剪的概念图（基于GAMES104 Lecture 04）
CROP_CONFIGS = {
    # name: {page: 0-based页码, box: (x0, y0, x1, y1)}
    "rendering_pipeline": {"page": 11, "box": (0.50, 0.15, 0.98, 0.95)},
    "mesh_primitive": {"page": 27, "box": (0.05, 0.15, 0.95, 0.95)},
    "materials_helmets": {"page": 30, "box": (0.10, 0.25, 0.90, 0.75)},
    "pbr_textures": {"page": 32, "box": (0.10, 0.20, 0.90, 0.85)},
    "submesh_structure": {"page": 37, "box": (0.05, 0.15, 0.98, 0.95)},
    "instance_reuse": {"page": 40, "box": (0.05, 0.20, 0.95, 0.90)},
    "frustum_culling": {"page": 45, "box": (0.05, 0.18, 0.95, 0.88)},
    "bvh_culling": {"page": 47, "box": (0.05, 0.18, 0.95, 0.88)},
    "block_compression": {"page": 55, "box": (0.45, 0.20, 0.95, 0.85)},
}

# 批量裁剪
batch_crop_concepts(
    pdf_path="GAMES104_Lecture04.pdf",
    output_dir="./GAMES104_Cropped",
    crop_configs=CROP_CONFIGS,
    zoom=2.0
)
```

## API 参考

### PDFImageToolkit 类

| 方法 | 说明 |
|------|------|
| `render_page(page_num, zoom, output_path)` | 渲染单页为PNG |
| `render_pages(page_numbers, output_dir, zoom)` | 批量渲染多页 |
| `crop_page(page_num, crop_box, zoom, output_path)` | 裁剪指定区域 |
| `extract_embedded_images(page_num, output_dir)` | 提取内嵌图片 |
| `get_page_text(page_num)` | 获取页面文本 |
| `get_page_info(page_num)` | 获取页面信息 |
| `analyze_content(sample_pages)` | 分析PDF内容分布 |

### 辅助函数

| 函数 | 说明 |
|------|------|
| `batch_crop_concepts(pdf_path, output_dir, crop_configs, zoom)` | 批量裁剪概念图 |

## 技巧与注意事项

1. **页码转换**: PDF页码通常1-based，但代码中使用0-based（第12页传入11）
2. **预览优先**: 先用`render_page`查看页面，确定裁剪坐标
3. **缩放选择**: `zoom=2.0`适用于1920x1080的PPT，可得到高清裁剪图
4. **文字识别**: 文字少的页面（`< 1000 chars`）通常是概念图页面
5. **批量配置**: 将裁剪配置保存为字典，便于复用和维护

## 文件位置

- 脚本: `<vault-root>/.agents/skills/pdf-image-extractor/pdf_toolkit.py`

# PDF 文本与代码提取 Skill

从 PDF 文档中提取文字内容、代码片段，支持 OCR 识别扫描版 PDF。

## 核心功能

| 功能 | 说明 |
|------|------|
| **文本提取** | 提取纯文本或保留布局格式的文本 |
| **代码识别** | 自动识别代码块、代码片段，支持多种语言 |
| **OCR 识别** | 对扫描版/图片版 PDF 进行文字识别 |
| **表格提取** | 提取 PDF 中的表格内容 |
| **文本搜索** | 搜索特定内容并返回所在页码和上下文 |

## 安装依赖

```bash
pip install PyMuPDF

# 如需 OCR 功能，额外安装：
pip install pytesseract pillow
# 并安装 Tesseract-OCR 引擎：https://github.com/UB-Mannheim/tesseract/wiki
```

## 快速开始

### 1. 提取纯文本

```python
from pdf_text_toolkit import PDFTextToolkit

with PDFTextToolkit("document.pdf") as toolkit:
    # 提取整本 PDF 的文本
    text = toolkit.extract_all_text()
    
    # 提取单页文本
    page_text = toolkit.extract_text(page_num=0)  # 第1页
    
    # 保留布局格式提取
    formatted_text = toolkit.extract_text_with_layout(page_num=0)
```

### 2. 提取代码片段

```python
with PDFTextToolkit("document.pdf") as toolkit:
    # 提取所有代码块
    code_blocks = toolkit.extract_code_blocks()
    
    # 提取特定页面的代码
    page_codes = toolkit.extract_code_blocks_from_page(page_num=5)
    
    # 搜索特定语言的代码
    python_codes = toolkit.search_code_by_language("python")
```

### 3. OCR 识别（扫描版 PDF）

```python
with PDFTextToolkit("scanned.pdf") as toolkit:
    # 对单页进行 OCR
    text = toolkit.ocr_page(page_num=0, lang="chi_sim+eng")
    
    # OCR 整个文档
    full_text = toolkit.ocr_all_pages(lang="eng")
```

### 4. 搜索文本

```python
with PDFTextToolkit("document.pdf") as toolkit:
    # 搜索关键词，返回所在页码和上下文
    results = toolkit.search_text("function", context_chars=100)
    # 返回: [{"page": 5, "text": "...", "matches": [...]}]
```

## 命令行使用

```bash
# 提取全部文本
python pdf_text_toolkit.py text "document.pdf" --output=content.txt

# 提取代码片段
python pdf_text_toolkit.py code "document.pdf" --output=code_snippets.json

# OCR 识别（扫描版 PDF）
python pdf_text_toolkit.py ocr "scanned.pdf" --lang=chi_sim+eng --output=ocr_result.txt

# 搜索文本
python pdf_text_toolkit.py search "document.pdf" "keyword" --context=100

# 提取特定页面
python pdf_text_toolkit.py text "document.pdf" --pages=1,3,5 --output=pages.txt
```

## 代码块识别规则

自动识别以下格式的代码：

| 特征 | 说明 |
|------|------|
| **缩进块** | 4空格或Tab缩进的连续行 |
| **等宽字体** | 使用 Monaco/Courier 等字体的段落 |
| **语法特征** | 包含关键字（def, class, if, for 等） |
| **边界标记** | 被空行包围的紧凑文本块 |

## 输出格式

### 代码片段 JSON 格式

```json
{
  "page": 5,
  "language": "python",
  "confidence": 0.85,
  "code": "def hello():\n    print('world')",
  "line_start": 10,
  "line_end": 12
}
```

## API 参考

| 方法 | 说明 |
|------|------|
| `extract_text(page_num)` | 提取单页纯文本 |
| `extract_all_text()` | 提取全部文本 |
| `extract_text_with_layout(page_num)` | 保留布局格式提取 |
| `extract_code_blocks()` | 提取所有代码块 |
| `extract_code_blocks_from_page(page_num)` | 提取单页代码块 |
| `search_code_by_language(lang)` | 按语言搜索代码 |
| `ocr_page(page_num, lang)` | OCR 识别单页 |
| `ocr_all_pages(lang)` | OCR 识别全部 |
| `search_text(keyword, context_chars)` | 搜索文本 |
| `extract_tables(page_num)` | 提取表格 |

## 文件位置

- 脚本: `<vault-root>/.agents/skills/pdf-text-extractor/pdf_text_toolkit.py`

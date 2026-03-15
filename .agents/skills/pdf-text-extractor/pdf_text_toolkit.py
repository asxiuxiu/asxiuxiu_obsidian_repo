#!/usr/bin/env python3
"""
PDF 文本与代码提取工具包
用于从 PDF 中提取文字内容、代码片段，支持 OCR 识别

依赖: PyMuPDF (fitz)
安装: pip install PyMuPDF

OCR 额外依赖:
    pip install pytesseract pillow
    并安装 Tesseract-OCR 引擎
"""

import fitz
import os
import re
import json
from pathlib import Path
from typing import List, Dict, Optional, Tuple

# 可选的 OCR 导入
try:
    import pytesseract
    from PIL import Image
    OCR_AVAILABLE = True
except ImportError:
    OCR_AVAILABLE = False


class PDFTextToolkit:
    """PDF 文本处理工具类"""
    
    # 代码语言识别关键字
    CODE_KEYWORDS = {
        'python': ['def ', 'class ', 'import ', 'from ', 'print(', 'self.', 'lambda ', '__init__'],
        'javascript': ['function', 'const ', 'let ', 'var ', '=>', 'document.', 'console.log'],
        'typescript': ['interface ', 'type ', 'export ', 'as ', ': ', '<T>'],
        'java': ['public ', 'private ', 'static ', 'void ', 'String ', 'System.'],
        'c': ['#include', 'int main', 'printf(', 'scanf(', 'void ', 'struct '],
        'cpp': ['#include', 'std::', 'cout <<', 'cin >>', 'template<', 'class ', 'public:'],
        'csharp': ['using ', 'namespace ', 'public class', 'private ', 'static ', 'void Main'],
        'go': ['package ', 'func ', 'import (', 'fmt.', 'defer ', 'goroutine'],
        'rust': ['fn ', 'let mut', 'impl ', 'struct ', 'use ', 'mod ', 'pub fn'],
        'bash': ['#!/bin/bash', 'echo ', 'export ', 'source ', '$', '|', 'grep ', 'awk '],
        'sql': ['SELECT ', 'FROM ', 'WHERE ', 'INSERT ', 'UPDATE ', 'DELETE ', 'JOIN '],
        'html': ['<!DOCTYPE', '<html', '<div', '<span', '<p>', 'class="', 'id="'],
        'css': ['{', '}', ': ', ';', 'px', 'em', 'rem', '@media', '.class', '#id'],
    }
    
    def __init__(self, pdf_path: str):
        """
        初始化工具包
        
        Args:
            pdf_path: PDF 文件路径
        """
        self.pdf_path = pdf_path
        self.doc = fitz.open(pdf_path)
        self.total_pages = len(self.doc)
        print(f"[PDF] 已加载: {pdf_path}")
        print(f"[PDF] 总页数: {self.total_pages}")
    
    def __enter__(self):
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
    
    def close(self):
        """关闭 PDF 文档"""
        if self.doc:
            self.doc.close()
            print("[PDF] 已关闭")
    
    # ==================== 基础文本提取 ====================
    
    def extract_text(self, page_num: int) -> str:
        """
        提取单页纯文本
        
        Args:
            page_num: 页码（从0开始）
        
        Returns:
            页面文本内容
        """
        if not self._check_page_num(page_num):
            return ""
        
        page = self.doc[page_num]
        text = page.get_text()
        return text.strip()
    
    def extract_all_text(self, page_range: Optional[Tuple[int, int]] = None) -> str:
        """
        提取全部或指定范围的文本
        
        Args:
            page_range: (start, end) 页码范围，None 表示全部
        
        Returns:
            合并后的文本内容
        """
        texts = []
        start, end = page_range if page_range else (0, self.total_pages)
        
        for page_num in range(start, min(end, self.total_pages)):
            text = self.extract_text(page_num)
            if text:
                texts.append(f"\n--- Page {page_num + 1} ---\n{text}")
        
        return "\n".join(texts)
    
    def extract_text_with_layout(self, page_num: int) -> str:
        """
        保留布局格式提取文本
        
        Args:
            page_num: 页码（从0开始）
        
        Returns:
            保留格式的文本内容
        """
        if not self._check_page_num(page_num):
            return ""
        
        page = self.doc[page_num]
        # 使用 dict 格式获取带布局信息的文本
        blocks = page.get_text("dict")["blocks"]
        
        lines = []
        for block in blocks:
            if "lines" in block:
                for line in block["lines"]:
                    line_text = ""
                    for span in line["spans"]:
                        line_text += span["text"]
                    if line_text.strip():
                        lines.append(line_text)
        
        return "\n".join(lines)
    
    # ==================== 代码片段提取 ====================
    
    def extract_code_blocks(self, min_lines: int = 3) -> List[Dict]:
        """
        从整个 PDF 中提取代码块
        
        Args:
            min_lines: 代码块最小行数
        
        Returns:
            代码块信息列表
        """
        all_blocks = []
        
        for page_num in range(self.total_pages):
            blocks = self.extract_code_blocks_from_page(page_num, min_lines)
            all_blocks.extend(blocks)
        
        return all_blocks
    
    def extract_code_blocks_from_page(self, page_num: int, min_lines: int = 3) -> List[Dict]:
        """
        从单页提取代码块
        
        Args:
            page_num: 页码（从0开始）
            min_lines: 代码块最小行数
        
        Returns:
            代码块信息列表
        """
        if not self._check_page_num(page_num):
            return []
        
        text = self.extract_text_with_layout(page_num)
        lines = text.split('\n')
        
        code_blocks = []
        current_block = []
        block_start = 0
        
        for i, line in enumerate(lines):
            is_code_line = self._is_likely_code_line(line)
            
            if is_code_line:
                if not current_block:
                    block_start = i
                current_block.append(line)
            else:
                if len(current_block) >= min_lines:
                    code_text = '\n'.join(current_block)
                    language = self._detect_language(code_text)
                    code_blocks.append({
                        "page": page_num + 1,
                        "language": language,
                        "confidence": self._calculate_confidence(code_text, language),
                        "code": code_text,
                        "line_start": block_start + 1,
                        "line_end": i
                    })
                current_block = []
        
        # 处理最后一块
        if len(current_block) >= min_lines:
            code_text = '\n'.join(current_block)
            language = self._detect_language(code_text)
            code_blocks.append({
                "page": page_num + 1,
                "language": language,
                "confidence": self._calculate_confidence(code_text, language),
                "code": code_text,
                "line_start": block_start + 1,
                "line_end": len(lines)
            })
        
        return code_blocks
    
    def search_code_by_language(self, language: str) -> List[Dict]:
        """
        搜索特定语言的代码块
        
        Args:
            language: 语言名称 (python, javascript, cpp, etc.)
        
        Returns:
            匹配的代码块列表
        """
        all_blocks = self.extract_code_blocks()
        return [b for b in all_blocks if b["language"] == language.lower()]
    
    def _is_likely_code_line(self, line: str) -> bool:
        """判断一行是否可能是代码"""
        line = line.rstrip()
        
        # 空行不是代码
        if not line:
            return False
        
        # 检查是否是普通文本段落（以中文开头、过长等）
        if len(line) > 100 and not any(c in line for c in '(){}[]=;<>"\''):
            return False
        
        # 代码特征检查
        code_patterns = [
            r'^\s{2,}',  # 缩进
            r'[=;{}()]',  # 代码符号
            r'^(def|class|if|for|while|return|import|from|const|let|var|function)',  # 关键字
            r'^(#|//|/\*|\*|\*/)',  # 注释
            r'[\{\}\[\]\(\)]',  # 括号
            r'->|=>|::',  # 箭头/作用域
        ]
        
        return any(re.search(pattern, line) for pattern in code_patterns)
    
    def _detect_language(self, code: str) -> str:
        """检测代码语言"""
        code_lower = code.lower()
        scores = {}
        
        for lang, keywords in self.CODE_KEYWORDS.items():
            score = sum(1 for kw in keywords if kw.lower() in code_lower)
            if score > 0:
                scores[lang] = score
        
        if scores:
            return max(scores, key=scores.get)
        return "unknown"
    
    def _calculate_confidence(self, code: str, language: str) -> float:
        """计算代码识别的置信度"""
        if language == "unknown":
            return 0.3
        
        keywords = self.CODE_KEYWORDS.get(language, [])
        matches = sum(1 for kw in keywords if kw.lower() in code.lower())
        lines = len(code.split('\n'))
        
        # 基于关键字匹配数和行数计算置信度
        confidence = min(0.95, (matches / max(len(keywords) * 0.3, 1)) * 0.7 + 
                              (min(lines, 20) / 20) * 0.3)
        return round(confidence, 2)
    
    # ==================== OCR 功能 ====================
    
    def ocr_page(self, page_num: int, lang: str = "eng") -> str:
        """
        对单页进行 OCR 识别
        
        Args:
            page_num: 页码（从0开始）
            lang: 语言代码 (eng, chi_sim, chi_tra, jpn, etc.)
        
        Returns:
            识别出的文本
        """
        if not OCR_AVAILABLE:
            raise ImportError("OCR 功能需要安装 pytesseract 和 pillow: pip install pytesseract pillow")
        
        if not self._check_page_num(page_num):
            return ""
        
        page = self.doc[page_num]
        
        # 渲染页面为图片
        mat = fitz.Matrix(2.0, 2.0)  # 2x 缩放以获得更好识别效果
        pix = page.get_pixmap(matrix=mat)
        
        # 转换为 PIL Image
        img = Image.frombytes("RGB", [pix.width, pix.height], pix.samples)
        
        # OCR 识别
        text = pytesseract.image_to_string(img, lang=lang)
        print(f"[OCR] 第 {page_num + 1} 页识别完成，提取 {len(text)} 字符")
        
        return text.strip()
    
    def ocr_all_pages(self, lang: str = "eng", output_path: Optional[str] = None) -> str:
        """
        OCR 识别整个 PDF
        
        Args:
            lang: 语言代码
            output_path: 可选的输出文件路径
        
        Returns:
            合并后的识别文本
        """
        if not OCR_AVAILABLE:
            raise ImportError("OCR 功能需要安装 pytesseract 和 pillow: pip install pytesseract pillow")
        
        texts = []
        for page_num in range(self.total_pages):
            print(f"[OCR] 正在处理第 {page_num + 1}/{self.total_pages} 页...")
            text = self.ocr_page(page_num, lang)
            if text:
                texts.append(f"\n--- Page {page_num + 1} ---\n{text}")
        
        full_text = "\n".join(texts)
        
        if output_path:
            with open(output_path, 'w', encoding='utf-8') as f:
                f.write(full_text)
            print(f"[OCR] 结果已保存: {output_path}")
        
        return full_text
    
    # ==================== 文本搜索 ====================
    
    def search_text(self, keyword: str, context_chars: int = 100, 
                    case_sensitive: bool = False) -> List[Dict]:
        """
        搜索 PDF 中的文本
        
        Args:
            keyword: 搜索关键词
            context_chars: 返回的上下文字符数
            case_sensitive: 是否区分大小写
        
        Returns:
            搜索结果列表
        """
        results = []
        flags = 0 if case_sensitive else re.IGNORECASE
        pattern = re.compile(re.escape(keyword), flags)
        
        for page_num in range(self.total_pages):
            text = self.extract_text(page_num)
            
            for match in pattern.finditer(text):
                start = max(0, match.start() - context_chars)
                end = min(len(text), match.end() + context_chars)
                context = text[start:end]
                
                # 高亮匹配词
                match_start_in_context = match.start() - start
                match_end_in_context = match.end() - start
                
                results.append({
                    "page": page_num + 1,
                    "match": match.group(),
                    "context": context,
                    "position": match.span()
                })
        
        print(f"[搜索] 找到 {len(results)} 处匹配: '{keyword}'")
        return results
    
    # ==================== 表格提取 ====================
    
    def extract_tables(self, page_num: int) -> List[List[List[str]]]:
        """
        提取页面中的表格
        
        Args:
            page_num: 页码（从0开始）
        
        Returns:
            表格数据列表（每个表格是一个二维列表）
        """
        if not self._check_page_num(page_num):
            return []
        
        page = self.doc[page_num]
        tables = page.find_tables()
        
        result = []
        for table in tables:
            result.append(table.extract())
        
        return result
    
    # ==================== 辅助方法 ====================
    
    def _check_page_num(self, page_num: int) -> bool:
        """检查页码是否有效"""
        if page_num < 0 or page_num >= self.total_pages:
            print(f"[错误] 页码 {page_num} 超出范围 [0, {self.total_pages - 1}]")
            return False
        return True
    
    def get_page_info(self, page_num: int) -> Dict:
        """获取页面信息"""
        if not self._check_page_num(page_num):
            return {}
        
        page = self.doc[page_num]
        text = page.get_text()
        
        return {
            "page": page_num + 1,
            "text_length": len(text),
            "has_text": len(text) > 0,
            "width": page.rect.width,
            "height": page.rect.height
        }


# ==================== 命令行入口 ====================

def main():
    import argparse
    
    parser = argparse.ArgumentParser(description="PDF 文本与代码提取工具")
    parser.add_argument("command", choices=["text", "code", "ocr", "search", "info"],
                       help="命令: text(提取文本), code(提取代码), ocr(OCR识别), search(搜索), info(信息)")
    parser.add_argument("pdf_path", help="PDF 文件路径")
    parser.add_argument("--output", "-o", help="输出文件路径")
    parser.add_argument("--pages", help="指定页码范围，如: 1,3,5-10")
    parser.add_argument("--lang", default="eng", help="OCR 语言 (默认: eng)")
    parser.add_argument("--context", type=int, default=100, help="搜索上下文字符数 (默认: 100)")
    parser.add_argument("--keyword", help="搜索关键词")
    parser.add_argument("--format", choices=["txt", "json"], default="txt", help="输出格式")
    
    args = parser.parse_args()
    
    with PDFTextToolkit(args.pdf_path) as toolkit:
        if args.command == "text":
            # 解析页码范围
            if args.pages:
                pages = _parse_page_range(args.pages)
                texts = []
                for p in pages:
                    if 0 <= p < toolkit.total_pages:
                        text = toolkit.extract_text(p)
                        texts.append(f"\n--- Page {p + 1} ---\n{text}")
                result = "\n".join(texts)
            else:
                result = toolkit.extract_all_text()
            
            _save_output(result, args.output)
            print(f"[完成] 文本已提取，共 {len(result)} 字符")
        
        elif args.command == "code":
            blocks = toolkit.extract_code_blocks()
            if args.format == "json":
                result = json.dumps(blocks, ensure_ascii=False, indent=2)
            else:
                lines = []
                for b in blocks:
                    lines.append(f"\n--- Page {b['page']} | {b['language']} (confidence: {b['confidence']}) ---")
                    lines.append(b['code'])
                    lines.append("")
                result = "\n".join(lines)
            
            _save_output(result, args.output)
            print(f"[完成] 提取了 {len(blocks)} 个代码块")
        
        elif args.command == "ocr":
            if not OCR_AVAILABLE:
                print("[错误] OCR 功能需要安装依赖: pip install pytesseract pillow")
                print("       并安装 Tesseract-OCR 引擎")
                return
            
            result = toolkit.ocr_all_pages(lang=args.lang)
            _save_output(result, args.output)
            print(f"[完成] OCR 识别完成")
        
        elif args.command == "search":
            if not args.keyword:
                print("[错误] 搜索需要提供 --keyword 参数")
                return
            
            results = toolkit.search_text(args.keyword, context_chars=args.context)
            
            if args.format == "json":
                output = json.dumps(results, ensure_ascii=False, indent=2)
            else:
                lines = [f"搜索 '{args.keyword}' 找到 {len(results)} 处匹配:\n"]
                for r in results:
                    lines.append(f"\n[第 {r['page']} 页]")
                    lines.append(f"上下文: ...{r['context']}...")
                    lines.append(f"匹配: {r['match']}")
                output = "\n".join(lines)
            
            _save_output(output, args.output)
            print(f"[完成] 找到 {len(results)} 处匹配")
        
        elif args.command == "info":
            info = {
                "path": args.pdf_path,
                "total_pages": toolkit.total_pages,
                "pages": [toolkit.get_page_info(i) for i in range(toolkit.total_pages)]
            }
            output = json.dumps(info, ensure_ascii=False, indent=2)
            _save_output(output, args.output)
            print(f"[完成] PDF 信息已获取")


def _parse_page_range(pages_str: str) -> List[int]:
    """解析页码范围字符串"""
    pages = []
    for part in pages_str.split(','):
        part = part.strip()
        if '-' in part:
            start, end = map(int, part.split('-'))
            pages.extend(range(start - 1, end))  # 转换为 0-based
        else:
            pages.append(int(part) - 1)  # 转换为 0-based
    return sorted(set(pages))


def _save_output(content: str, output_path: Optional[str]):
    """保存输出到文件或打印"""
    if output_path:
        with open(output_path, 'w', encoding='utf-8') as f:
            f.write(content)
        print(f"[保存] 输出已保存: {output_path}")
    else:
        print("\n" + "="*50)
        print(content)
        print("="*50)


if __name__ == "__main__":
    main()

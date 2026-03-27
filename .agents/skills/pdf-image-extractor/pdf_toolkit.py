#!/usr/bin/env python3
"""
PDF 插图处理工具包
用于提取、渲染和裁剪 PDF 中的概念图和示意图

依赖: PyMuPDF (fitz)
安装: pip install PyMuPDF
"""

import fitz
import os
from pathlib import Path


class PDFImageToolkit:
    """PDF 图片处理工具类"""
    
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
    
    def render_page(self, page_num: int, zoom: float = 2.0, output_path: str = None) -> str:
        """
        渲染单页为 PNG 图片
        
        Args:
            page_num: 页码（从0开始）
            zoom: 缩放倍数（默认2.0，即1920x1080分辨率）
            output_path: 输出路径（默认自动生成）
        
        Returns:
            输出文件的完整路径
        """
        if page_num < 0 or page_num >= self.total_pages:
            raise ValueError(f"页码 {page_num} 超出范围 [0, {self.total_pages-1}]")
        
        page = self.doc[page_num]
        mat = fitz.Matrix(zoom, zoom)
        pix = page.get_pixmap(matrix=mat)
        
        if output_path is None:
            output_path = f"page_{page_num+1:02d}.png"
        
        pix.save(output_path)
        print(f"[渲染] 已保存: {output_path} ({pix.width}x{pix.height})")
        return output_path
    
    def render_pages(self, page_numbers: list, output_dir: str = "./pdf_images", zoom: float = 2.0) -> list:
        """
        批量渲染多页
        
        Args:
            page_numbers: 页码列表（从0开始）
            output_dir: 输出目录
            zoom: 缩放倍数
        
        Returns:
            生成的文件路径列表
        """
        os.makedirs(output_dir, exist_ok=True)
        output_paths = []
        
        for page_num in page_numbers:
            output_path = os.path.join(output_dir, f"page_{page_num+1:02d}.png")
            self.render_page(page_num, zoom, output_path)
            output_paths.append(output_path)
        
        return output_paths
    
    def extract_embedded_images(self, page_num: int, output_dir: str = "./extracted_images") -> list:
        """
        提取页面中内嵌的图片资源
        
        Args:
            page_num: 页码（从0开始）
            output_dir: 输出目录
        
        Returns:
            提取的图片文件路径列表
        """
        os.makedirs(output_dir, exist_ok=True)
        page = self.doc[page_num]
        images = page.get_images(full=True)
        
        extracted_paths = []
        for img_idx, img in enumerate(images):
            xref = img[0]
            try:
                base_image = self.doc.extract_image(xref)
                image_bytes = base_image["image"]
                ext = base_image["ext"]
                
                img_path = os.path.join(output_dir, f"page_{page_num+1}_img{img_idx+1}.{ext}")
                with open(img_path, "wb") as f:
                    f.write(image_bytes)
                extracted_paths.append(img_path)
                print(f"[提取] {img_path}")
            except Exception as e:
                print(f"[警告] 提取图片失败: {e}")
        
        return extracted_paths
    
    def crop_page(self, page_num: int, crop_box: tuple, zoom: float = 2.0, output_path: str = None) -> str:
        """
        裁剪页面指定区域
        
        Args:
            page_num: 页码（从0开始）
            crop_box: 裁剪区域 (x0, y0, x1, y1)，坐标为页面宽高的比例 (0.0-1.0)
            zoom: 缩放倍数
            output_path: 输出路径
        
        Returns:
            输出文件的完整路径
        
        Example:
            # 裁剪页面右侧区域（常用于PPT的概念图）
            toolkit.crop_page(11, (0.50, 0.15, 0.98, 0.95), output_path="diagram.png")
        """
        if page_num < 0 or page_num >= self.total_pages:
            raise ValueError(f"页码 {page_num} 超出范围 [0, {self.total_pages-1}]")
        
        page = self.doc[page_num]
        rect = page.rect
        
        # 比例坐标转绝对坐标
        x0 = crop_box[0] * rect.width
        y0 = crop_box[1] * rect.height
        x1 = crop_box[2] * rect.width
        y1 = crop_box[3] * rect.height
        
        clip = fitz.Rect(x0, y0, x1, y1)
        mat = fitz.Matrix(zoom, zoom)
        pix = page.get_pixmap(matrix=mat, clip=clip)
        
        if output_path is None:
            output_path = f"page_{page_num+1}_cropped.png"
        
        pix.save(output_path)
        print(f"[裁剪] 已保存: {output_path} ({pix.width}x{pix.height})")
        return output_path
    
    def get_page_text(self, page_num: int) -> str:
        """获取页面文本内容"""
        page = self.doc[page_num]
        return page.get_text()
    
    def get_page_info(self, page_num: int) -> dict:
        """获取页面信息（文本长度、图片数量等）"""
        page = self.doc[page_num]
        images = page.get_images()
        return {
            "page": page_num + 1,
            "text_length": len(page.get_text()),
            "image_count": len(images),
            "width": page.rect.width,
            "height": page.rect.height
        }
    
    def analyze_content(self, sample_pages: int = 10) -> list:
        """
        分析 PDF 内容，返回关键页面信息
        
        Args:
            sample_pages: 采样的页面数（均匀分布）
        
        Returns:
            页面信息列表
        """
        step = max(1, self.total_pages // sample_pages)
        pages_to_check = list(range(0, self.total_pages, step))[:sample_pages]
        
        results = []
        for page_num in pages_to_check:
            info = self.get_page_info(page_num)
            results.append(info)
            print(f"[分析] Page {info['page']}: {info['text_length']} chars, {info['image_count']} images")
        
        return results


# ==================== 预设裁剪配置 ====================

# GAMES104 Lecture 04 的裁剪配置（作为示例）
GAMES104_CROPS = {
    "rendering_pipeline": {"page": 11, "box": (0.50, 0.15, 0.98, 0.95)},
    "mesh_primitive": {"page": 27, "box": (0.05, 0.15, 0.95, 0.95)},
    "materials_helmets": {"page": 30, "box": (0.10, 0.25, 0.90, 0.75)},
    "pbr_textures": {"page": 32, "box": (0.10, 0.20, 0.90, 0.85)},
    "submesh_structure": {"page": 37, "box": (0.05, 0.15, 0.98, 0.95)},
    "instance_reuse": {"page": 40, "box": (0.05, 0.20, 0.95, 0.90)},
    "frustum_culling": {"page": 45, "box": (0.05, 0.18, 0.95, 0.88)},
    "bvh_culling": {"page": 47, "box": (0.05, 0.18, 0.95, 0.88)},
    "pvs_grid": {"page": 50, "box": (0.55, 0.25, 0.95, 0.75)},
    "block_compression": {"page": 55, "box": (0.45, 0.20, 0.95, 0.85)},
}


def batch_crop_concepts(pdf_path: str, output_dir: str, crop_configs: dict, zoom: float = 2.0):
    """
    批量裁剪概念图
    
    Args:
        pdf_path: PDF 文件路径
        output_dir: 输出目录
        crop_configs: 裁剪配置字典 {name: {page: int, box: tuple}}
        zoom: 缩放倍数
    
    Example:
        from pdf_toolkit import batch_crop_concepts, GAMES104_CROPS
        
        batch_crop_concepts(
            "GAMES104_Lecture04.pdf",
            "./cropped_images",
            GAMES104_CROPS
        )
    """
    os.makedirs(output_dir, exist_ok=True)
    
    with PDFImageToolkit(pdf_path) as toolkit:
        for name, config in crop_configs.items():
            page_num = config["page"]
            box = config["box"]
            output_path = os.path.join(output_dir, f"{name}.png")
            
            try:
                toolkit.crop_page(page_num, box, zoom, output_path)
            except Exception as e:
                print(f"[错误] 裁剪 {name} 失败: {e}")


# ==================== 命令行入口 ====================

if __name__ == "__main__":
    import sys
    
    if len(sys.argv) < 2:
        print("""
PDF 插图处理工具

用法:
  1. 渲染指定页面:
     python pdf_toolkit.py render <pdf_path> <page_num> [zoom]
     
  2. 提取内嵌图片:
     python pdf_toolkit.py extract <pdf_path> <page_num> [output_dir]
     
  3. 裁剪指定区域:
     python pdf_toolkit.py crop <pdf_path> <page_num> <x0> <y0> <x1> <y1> [output_path]
     
  4. 分析 PDF 内容:
     python pdf_toolkit.py analyze <pdf_path> [sample_pages]

示例:
  python pdf_toolkit.py render "lecture.pdf" 10
  python pdf_toolkit.py crop "lecture.pdf" 11 0.5 0.15 0.98 0.95 diagram.png
        """)
        sys.exit(1)
    
    command = sys.argv[1]
    pdf_path = sys.argv[2]
    
    with PDFImageToolkit(pdf_path) as toolkit:
        if command == "render":
            page_num = int(sys.argv[3]) - 1  # 用户输入1-based，转换为0-based
            zoom = float(sys.argv[4]) if len(sys.argv) > 4 else 2.0
            toolkit.render_page(page_num, zoom)
            
        elif command == "extract":
            page_num = int(sys.argv[3]) - 1
            output_dir = sys.argv[4] if len(sys.argv) > 4 else "./extracted"
            toolkit.extract_embedded_images(page_num, output_dir)
            
        elif command == "crop":
            page_num = int(sys.argv[3]) - 1
            x0, y0, x1, y1 = map(float, sys.argv[4:8])
            output_path = sys.argv[8] if len(sys.argv) > 8 else None
            toolkit.crop_page(page_num, (x0, y0, x1, y1), 2.0, output_path)
            
        elif command == "analyze":
            sample_pages = int(sys.argv[3]) if len(sys.argv) > 3 else 10
            toolkit.analyze_content(sample_pages)
            
        else:
            print(f"未知命令: {command}")

#!/usr/bin/env python3
"""Check broken image files and find their references in notes"""
import os
from pathlib import Path

# The 5 broken files
broken_files = [
    'Assets/games/bvh_culling.png',
    'Assets/games/gpu_instancing.png',
    'Assets/games/pbr_textures.jpg',
    'Assets/games/submesh_structure.png',
    'Assets/games/images/ndf_distribution.png'
]

def check_file_content(filepath):
    """Check if file is HTML error page"""
    try:
        with open(filepath, 'rb') as f:
            content = f.read(5000).decode('utf-8', errors='ignore').lower()
            
        is_html = '<html' in content or '<!doctype' in content
        has_error = '404' in content or 'error' in content or 'not found' in content or 'nginx' in content or 'cloudflare' in content
        
        # Get title if possible
        title = "Unknown"
        if '<title>' in content:
            start = content.find('<title>') + 7
            end = content.find('</title>', start)
            if end > start:
                title = content[start:end].strip()
        
        return is_html, has_error, title[:100]
    except Exception as e:
        return False, False, str(e)

def find_references(filename):
    """Find which markdown files reference this image"""
    refs = []
    vault_root = Path('.')
    
    for md_file in vault_root.rglob('*.md'):
        # Skip in certain directories
        if '.git' in str(md_file):
            continue
        try:
            with open(md_file, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
                if filename in content:
                    refs.append(str(md_file))
        except:
            pass
    
    return refs

print("=" * 70)
print("检查损坏的图片文件")
print("=" * 70)

for filepath in broken_files:
    print(f"\n[FILE] {filepath}")
    print("-" * 70)
    
    # Check if file exists
    if not os.path.exists(filepath):
        print("  [ERROR] 文件不存在")
        continue
    
    # Check content
    is_html, has_error, title = check_file_content(filepath)
    
    if is_html:
        print(f"  [WARN] 实际为 HTML 文件")
        print(f"  [TITLE] 页面标题: {title}")
        if has_error:
            print(f"  [ERROR_PAGE] 疑似错误页面 (包含 404/Error 关键字)")
    
    # Find references
    filename = Path(filepath).name
    refs = find_references(filename)
    
    if refs:
        print(f"  [REFS] 被以下笔记引用:")
        for ref in refs[:5]:  # Show max 5
            print(f"     - {ref}")
        if len(refs) > 5:
            print(f"     ... 还有 {len(refs) - 5} 个引用")
    else:
        print(f"  [OK] 未被任何笔记引用")

print("\n" + "=" * 70)
print("建议: 以上文件可以安全删除")
print("=" * 70)

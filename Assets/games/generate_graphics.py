#!/usr/bin/env python3
"""
生成高质量游戏引擎渲染系统技术图表
"""

import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.patches import FancyBboxPatch, Rectangle, FancyArrowPatch, Circle
import numpy as np
import os

# 设置中文字体支持
plt.rcParams['font.sans-serif'] = ['Segoe UI', 'Arial Unicode MS', 'DejaVu Sans']
plt.rcParams['axes.unicode_minus'] = False

def save_fig(name, dpi=200):
    """保存图片并清理"""
    plt.tight_layout()
    plt.savefig(f'{name}', dpi=dpi, bbox_inches='tight', facecolor='white', edgecolor='none')
    plt.close()
    print(f"Generated: {name}")

def create_rendering_pipeline():
    """1. 渲染管线流程图"""
    fig, ax = plt.subplots(figsize=(14, 8))
    ax.set_xlim(0, 14)
    ax.set_ylim(0, 8)
    ax.axis('off')
    
    # 标题
    ax.text(7, 7.5, 'Modern Rendering Pipeline', fontsize=20, fontweight='bold', 
            ha='center', va='center', color='#1a1a2e')
    
    # 管线阶段
    stages = [
        ('Vertex\nData', 1, '#e8f4f8'),
        ('Vertex\nShader', 3, '#b8e6f0'),
        ('Tessellation\n(Optional)', 5, '#d4edda'),
        ('Geometry\nShader', 7, '#fff3cd'),
        ('Rasterization', 9, '#f8d7da'),
        ('Fragment\nShader', 11, '#e2d4f0'),
        ('Output\nMerger', 13, '#cce5ff')
    ]
    
    for name, x, color in stages:
        box = FancyBboxPatch((x-0.7, 3.5), 1.4, 2.2, boxstyle="round,pad=0.1", 
                             facecolor=color, edgecolor='#333', linewidth=2)
        ax.add_patch(box)
        ax.text(x, 4.6, name, ha='center', va='center', fontsize=10, fontweight='bold')
    
    # 箭头
    for i in range(len(stages)-1):
        ax.annotate('', xy=(stages[i+1][1]-0.8, 4.6), xytext=(stages[i][1]+0.8, 4.6),
                   arrowprops=dict(arrowstyle='->', color='#666', lw=2))
    
    # 数据流标注
    ax.text(7, 2.5, 'Programmable Stages', fontsize=12, ha='center', color='#28a745', fontweight='bold')
    ax.text(7, 2.0, 'Vertex Shader → Fragment Shader', fontsize=10, ha='center', color='#666')
    ax.text(7, 1.5, 'Fixed Function: Rasterization, Depth Test', fontsize=10, ha='center', color='#666')
    
    save_fig('rendering_pipeline.png')

def create_mesh_primitive():
    """2. 网格顶点结构与索引缓冲"""
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 7))
    
    # 左侧：原始三角形存储
    ax1.set_xlim(0, 10)
    ax1.set_ylim(0, 10)
    ax1.axis('off')
    ax1.set_title('Direct Triangle Storage (Redundant)', fontsize=14, fontweight='bold', pad=20)
    
    # 绘制立方体线框
    cube_verts = np.array([
        [2, 2], [5, 2], [5, 5], [2, 5],  # front
        [3, 3], [6, 3], [6, 6], [3, 6]   # back
    ])
    edges = [(0,1), (1,2), (2,3), (3,0), (4,5), (5,6), (6,7), (7,4),
             (0,4), (1,5), (2,6), (3,7)]
    
    for edge in edges:
        ax1.plot([cube_verts[edge[0]][0], cube_verts[edge[1]][0]],
                [cube_verts[edge[0]][1], cube_verts[edge[1]][1]], 'b-', linewidth=2, alpha=0.6)
    
    ax1.text(5, 0.5, '36 vertices (12 triangles × 3)\nMemory: HIGH', 
             ha='center', fontsize=11, color='#dc3545', fontweight='bold',
             bbox=dict(boxstyle='round', facecolor='#f8d7da', edgecolor='#dc3545'))
    
    # 右侧：索引缓冲优化
    ax2.set_xlim(0, 10)
    ax2.set_ylim(0, 10)
    ax2.axis('off')
    ax2.set_title('Index Buffer Optimization', fontsize=14, fontweight='bold', pad=20)
    
    # 绘制同样的立方体
    for edge in edges:
        ax2.plot([cube_verts[edge[0]][0], cube_verts[edge[1]][0]],
                [cube_verts[edge[0]][1], cube_verts[edge[1]][1]], 'g-', linewidth=2, alpha=0.6)
    
    # 顶点标记
    for i, (x, y) in enumerate(cube_verts):
        color = '#28a745' if i < 4 else '#17a2b8'
        ax2.scatter(x, y, s=100, c=color, zorder=5, edgecolors='white', linewidths=2)
        ax2.text(x, y, f'V{i}', ha='center', va='center', fontsize=8, color='white', fontweight='bold')
    
    ax2.text(5, 0.5, '8 unique vertices + 36 indices\nMemory: 78% SAVED', 
             ha='center', fontsize=11, color='#28a745', fontweight='bold',
             bbox=dict(boxstyle='round', facecolor='#d4edda', edgecolor='#28a745'))
    
    save_fig('mesh_primitive.png')

def create_materials_pbr():
    """3. PBR材质头盔对比 - 简化为材质属性说明图"""
    fig, ax = plt.subplots(figsize=(14, 8))
    ax.set_xlim(0, 14)
    ax.set_ylim(0, 10)
    ax.axis('off')
    
    ax.text(7, 9.5, 'PBR Material Properties', fontsize=18, fontweight='bold', ha='center')
    
    materials = [
        ('Smooth Metal', 2, 7.5, '#c0c0c0', 'High Reflectivity', 'Metallic: 1.0\nRoughness: 0.1'),
        ('Glossy Paint', 5.5, 7.5, '#e74c3c', 'Specular Highlight', 'Metallic: 0.0\nRoughness: 0.2'),
        ('Rough Stone', 9, 7.5, '#8b7355', 'Diffuse Scatter', 'Metallic: 0.0\nRoughness: 0.9'),
        ('Matte Plastic', 12.5, 7.5, '#3498db', 'Even Diffusion', 'Metallic: 0.0\nRoughness: 0.5'),
    ]
    
    for name, x, y, color, desc, props in materials:
        # 材质球
        circle = Circle((x, y), 0.8, facecolor=color, edgecolor='#333', linewidth=2)
        ax.add_patch(circle)
        
        # 高光效果（模拟）
        highlight = Circle((x-0.3, y+0.3), 0.2, facecolor='white', alpha=0.4)
        ax.add_patch(highlight)
        
        ax.text(x, y-1.5, name, ha='center', fontsize=11, fontweight='bold')
        ax.text(x, y-2.2, desc, ha='center', fontsize=9, color='#666')
        ax.text(x, y-3.0, props, ha='center', fontsize=8, color='#495057', 
                family='monospace', bbox=dict(boxstyle='round', facecolor='#f8f9fa'))
    
    # 底部说明
    ax.text(7, 2, 'Physically Based Rendering (PBR)', fontsize=14, fontweight='bold', 
            ha='center', color='#1a1a2e')
    ax.text(7, 1.2, 'Energy Conservation | Microfacet Theory | Fresnel Effect', 
            fontsize=11, ha='center', color='#6c757d')
    
    save_fig('materials_helmets.png')

def create_pbr_textures():
    """4. PBR贴图组成"""
    fig, ax = plt.subplots(figsize=(14, 9))
    ax.set_xlim(0, 14)
    ax.set_ylim(0, 10)
    ax.axis('off')
    
    ax.text(7, 9.5, 'PBR Texture Maps Workflow', fontsize=18, fontweight='bold', ha='center')
    
    # 贴图类型
    textures = [
        ('Albedo\n(Base Color)', 2, 7, '#ff6b6b', 'Surface Color\n(No Lighting)'),
        ('Metallic', 4.5, 7, '#ffd93d', 'Metal vs Non-Metal\n(White = Metal)'),
        ('Roughness', 7, 7, '#6bcf7f', 'Surface Roughness\n(White = Rough)'),
        ('Normal', 9.5, 7, '#4d96ff', 'Surface Detail\n(Bump Mapping)'),
        ('AO', 12, 7, '#9b59b6', 'Ambient Occlusion\n(Crevices Shadow)'),
    ]
    
    for name, x, y, color, desc in textures:
        # 贴图方块
        rect = FancyBboxPatch((x-0.8, y-0.8), 1.6, 1.6, boxstyle="round,pad=0.05",
                              facecolor=color, edgecolor='#333', linewidth=2)
        ax.add_patch(rect)
        
        ax.text(x, y, name, ha='center', va='center', fontsize=10, fontweight='bold', color='white')
        ax.text(x, y-1.5, desc, ha='center', fontsize=9, color='#495057')
    
    # 箭头指向组合
    ax.annotate('', xy=(7, 4.5), xytext=(2, 5.5),
               arrowprops=dict(arrowstyle='->', color='#666', lw=2, connectionstyle='arc3,rad=0.1'))
    ax.annotate('', xy=(7, 4.5), xytext=(4.5, 5.5),
               arrowprops=dict(arrowstyle='->', color='#666', lw=2, connectionstyle='arc3,rad=0.05'))
    ax.annotate('', xy=(7, 4.5), xytext=(7, 5.5),
               arrowprops=dict(arrowstyle='->', color='#666', lw=2))
    ax.annotate('', xy=(7, 4.5), xytext=(9.5, 5.5),
               arrowprops=dict(arrowstyle='->', color='#666', lw=2, connectionstyle='arc3,rad=-0.05'))
    ax.annotate('', xy=(7, 4.5), xytext=(12, 5.5),
               arrowprops=dict(arrowstyle='->', color='#666', lw=2, connectionstyle='arc3,rad=-0.1'))
    
    # 最终材质
    final_box = FancyBboxPatch((4.5, 2.5), 5, 2, boxstyle="round,pad=0.1",
                               facecolor='#e8f4f8', edgecolor='#333', linewidth=3)
    ax.add_patch(final_box)
    ax.text(7, 3.5, 'Final Material', ha='center', fontsize=14, fontweight='bold')
    ax.text(7, 2.9, 'PBR Shader combines all maps', ha='center', fontsize=10, color='#666')
    
    save_fig('pbr_textures.png')

def create_submesh_structure():
    """5. Submesh 结构"""
    fig, ax = plt.subplots(figsize=(14, 8))
    ax.set_xlim(0, 14)
    ax.set_ylim(0, 10)
    ax.axis('off')
    
    ax.text(7, 9.5, 'Mesh with Multiple Submeshes', fontsize=18, fontweight='bold', ha='center')
    
    # 主网格框
    main_box = FancyBboxPatch((1, 4), 12, 4.5, boxstyle="round,pad=0.1",
                              facecolor='#f8f9fa', edgecolor='#333', linewidth=2)
    ax.add_patch(main_box)
    ax.text(1.5, 8, 'Character Mesh', fontsize=12, fontweight='bold', color='#495057')
    
    # Submesh 划分
    submeshes = [
        ('Submesh 0\nHelmet', 2.5, 6, '#e74c3c', 'Shader A'),
        ('Submesh 1\nSkin', 5, 6, '#f39c12', 'Shader B'),
        ('Submesh 2\nArmor', 7.5, 6, '#3498db', 'Shader C'),
        ('Submesh 3\nBoots', 10, 6, '#9b59b6', 'Shader D'),
    ]
    
    for name, x, y, color, shader in submeshes:
        box = FancyBboxPatch((x-1, y-1.2), 2, 2.2, boxstyle="round,pad=0.05",
                             facecolor=color, edgecolor='#333', linewidth=2, alpha=0.7)
        ax.add_patch(box)
        ax.text(x, y+0.3, name, ha='center', va='center', fontsize=10, fontweight='bold', color='white')
        ax.text(x, y-0.8, shader, ha='center', va='center', fontsize=9, color='white')
    
    # 共享缓冲区说明
    ax.text(7, 3, 'Shared Vertex/Index Buffer', fontsize=12, fontweight='bold', 
            ha='center', color='#28a745')
    ax.text(7, 2.3, 'Each submesh uses offset + count to reference data', 
            fontsize=10, ha='center', color='#6c757d')
    
    # 优势
    benefits = ['✓ Single Draw Call per Material', '✓ Easy Skin/Equipment Swapping', '✓ Memory Efficient']
    for i, benefit in enumerate(benefits):
        ax.text(7, 1.5 - i*0.5, benefit, ha='center', fontsize=10, color='#495057')
    
    save_fig('submesh_structure.png')

def create_instance_reuse():
    """6. 资源实例化"""
    fig, ax = plt.subplots(figsize=(14, 8))
    ax.set_xlim(0, 14)
    ax.set_ylim(0, 10)
    ax.axis('off')
    
    ax.text(7, 9.5, 'Resource Pool & Instance Reuse', fontsize=18, fontweight='bold', ha='center')
    
    # 资源池
    pool_box = FancyBboxPatch((1, 5), 4, 3.5, boxstyle="round,pad=0.1",
                              facecolor='#e3f2fd', edgecolor='#1976d2', linewidth=3)
    ax.add_patch(pool_box)
    ax.text(3, 8, 'Resource Pool', fontsize=14, fontweight='bold', ha='center', color='#1976d2')
    
    resources = ['Mesh Data', 'Shader Code', 'Texture Data', 'Material Template']
    for i, res in enumerate(resources):
        y = 7.2 - i * 0.6
        rect = Rectangle((1.5, y-0.2), 3, 0.4, facecolor='#bbdefb', edgecolor='#1976d2')
        ax.add_patch(rect)
        ax.text(3, y, res, ha='center', va='center', fontsize=9, color='#0d47a1')
    
    # 实例
    for i, (x, label) in enumerate([(7, 'Instance 1'), (10, 'Instance 2'), (12.5, 'Instance N')]):
        inst_box = FancyBboxPatch((x-1.2, 4.5), 2.4, 4, boxstyle="round,pad=0.1",
                                  facecolor='#fff3e0', edgecolor='#f57c00', linewidth=2)
        ax.add_patch(inst_box)
        ax.text(x, 8.2, label, fontsize=11, fontweight='bold', ha='center', color='#e65100')
        
        # 引用箭头
        ax.annotate('', xy=(x, 7.5), xytext=(5, 7.5),
                   arrowprops=dict(arrowstyle='->', color='#666', lw=1.5, 
                                  connectionstyle=f'arc3,rad={0.1-i*0.1}'))
        
        # 实例数据
        data_items = ['Handle Reference', 'Transform Matrix', 'Unique Color']
        for j, item in enumerate(data_items):
            y = 6.8 - j * 0.6
            rect = Rectangle((x-0.9, y-0.2), 1.8, 0.4, facecolor='#ffe0b2', edgecolor='#f57c00')
            ax.add_patch(rect)
            ax.text(x, y, item, ha='center', va='center', fontsize=8, color='#e65100')
    
    # 底部说明
    ax.text(7, 2.5, '1000 Soldiers = 1 Mesh + N Transforms', fontsize=13, 
            fontweight='bold', ha='center', color='#2e7d32')
    ax.text(7, 1.8, 'Instead of 1000× full mesh data copies', fontsize=11, 
            ha='center', color='#666')
    
    save_fig('instance_reuse.png')

def create_frustum_culling():
    """7. 视锥剔除"""
    fig, ax = plt.subplots(figsize=(14, 8))
    ax.set_xlim(0, 14)
    ax.set_ylim(0, 10)
    ax.axis('off')
    
    ax.text(7, 9.5, 'View Frustum Culling', fontsize=18, fontweight='bold', ha='center')
    
    # 绘制视锥体（梯形）
    frustum_x = [3, 11, 10, 4]
    frustum_y = [2, 2, 7, 7]
    ax.fill(frustum_x, frustum_y, alpha=0.2, color='#4CAF50', edgecolor='#2E7D32', linewidth=2)
    
    # 相机位置
    camera = Circle((2, 4.5), 0.5, facecolor='#333', edgecolor='#000', linewidth=2)
    ax.add_patch(camera)
    ax.text(2, 3.5, 'Camera', ha='center', fontsize=10, fontweight='bold')
    
    # 视锥平面标注
    ax.text(7, 7.3, 'Far Plane', ha='center', fontsize=9, color='#2E7D32')
    ax.text(6.5, 1.7, 'Near Plane', ha='center', fontsize=9, color='#2E7D32')
    
    # 物体
    objects = [
        (5, 4.5, '#4CAF50', 'Inside\nRendered'),
        (8, 5, '#4CAF50', 'Inside\nRendered'),
        (12, 5, '#f44336', 'Outside\nCulled'),
        (6, 8.5, '#f44336', 'Outside\nCulled'),
    ]
    
    for x, y, color, label in objects:
        rect = Rectangle((x-0.6, y-0.6), 1.2, 1.2, facecolor=color, 
                         edgecolor='#333', linewidth=2, alpha=0.7)
        ax.add_patch(rect)
        ax.text(x, y, label, ha='center', va='center', fontsize=8, 
                color='white', fontweight='bold')
    
    # 视线箭头
    ax.annotate('', xy=(7, 4.5), xytext=(2.5, 4.5),
               arrowprops=dict(arrowstyle='->', color='#2196F3', lw=2))
    ax.text(4.5, 5, 'View Direction', fontsize=10, color='#2196F3')
    
    # 统计
    ax.text(7, 1, 'Frustum Test: 6 Planes (Near/Far/Left/Right/Top/Bottom)', 
            fontsize=11, ha='center', color='#666')
    
    save_fig('frustum_culling.png')

def create_bvh_culling():
    """8. BVH 层级剔除"""
    fig, ax = plt.subplots(figsize=(14, 9))
    ax.set_xlim(0, 14)
    ax.set_ylim(0, 10)
    ax.axis('off')
    
    ax.text(7, 9.5, 'Bounding Volume Hierarchy (BVH) Culling', fontsize=18, fontweight='bold', ha='center')
    
    # 树结构
    # 根节点
    root = FancyBboxPatch((6, 7.5), 2, 1, boxstyle="round,pad=0.1",
                          facecolor='#e3f2fd', edgecolor='#1976d2', linewidth=2)
    ax.add_patch(root)
    ax.text(7, 8, 'Root\n(Scene)', ha='center', va='center', fontsize=10, fontweight='bold')
    
    # 第二层
    level2 = [(3.5, 'Left'), (10.5, 'Right')]
    for x, label in level2:
        box = FancyBboxPatch((x-1, 5.5), 2, 1, boxstyle="round,pad=0.1",
                             facecolor='#fff3e0', edgecolor='#f57c00', linewidth=2)
        ax.add_patch(box)
        ax.text(x, 6, f'{label}\nBranch', ha='center', va='center', fontsize=10)
        ax.plot([7, x], [7.5, 6.5], 'k-', linewidth=1.5)
    
    # 第三层 - 叶子节点
    leaves = [
        (2, 'A', '#4CAF50', 'Visible'),
        (5, 'B', '#f44336', 'Culled'),
        (9, 'C', '#4CAF50', 'Visible'),
        (12, 'D', '#4CAF50', 'Visible'),
    ]
    
    for x, label, color, status in leaves:
        box = FancyBboxPatch((x-0.9, 3), 1.8, 1, boxstyle="round,pad=0.1",
                             facecolor=color, edgecolor='#333', linewidth=2, alpha=0.7)
        ax.add_patch(box)
        ax.text(x, 3.5, f'Object {label}\n{status}', ha='center', va='center', 
                fontsize=9, color='white', fontweight='bold')
        
        # 连接线
        parent_x = 3.5 if x < 7 else 10.5
        ax.plot([parent_x, x], [5.5, 4], 'k-', linewidth=1.5)
    
    # 图例
    legend_items = [
        ('#4CAF50', 'Inside Frustum'),
        ('#f44336', 'Outside Frustum'),
    ]
    for i, (color, label) in enumerate(legend_items):
        rect = Rectangle((9.5 + i*2, 1.5), 0.4, 0.4, facecolor=color, edgecolor='#333')
        ax.add_patch(rect)
        ax.text(10 + i*2, 1.7, label, fontsize=10, va='center')
    
    # 说明
    ax.text(7, 0.8, 'Hierarchical Traversal: Skip entire subtree if parent is culled', 
            fontsize=11, ha='center', color='#666', style='italic')
    
    save_fig('bvh_culling.png')

def create_block_compression():
    """9. 纹理块压缩"""
    fig, ax = plt.subplots(figsize=(14, 8))
    ax.set_xlim(0, 14)
    ax.set_ylim(0, 10)
    ax.axis('off')
    
    ax.text(7, 9.5, 'Block Compression (BC/ASTC)', fontsize=18, fontweight='bold', ha='center')
    
    # 原始 4x4 块
    ax.text(3.5, 8, 'Original 4×4 Block', fontsize=12, fontweight='bold', ha='center')
    for i in range(4):
        for j in range(4):
            color_val = 0.3 + 0.5 * ((i + j) / 6)
            rect = Rectangle((2 + i*0.7, 5.5 + j*0.7), 0.65, 0.65, 
                            facecolor=plt.cm.viridis(color_val), edgecolor='#333')
            ax.add_patch(rect)
    
    # 箭头
    ax.annotate('', xy=(7, 6.5), xytext=(5, 6.5),
               arrowprops=dict(arrowstyle='->', color='#666', lw=3))
    ax.text(6, 6.9, 'Compress', fontsize=11, ha='center', fontweight='bold')
    
    # 压缩后
    ax.text(10.5, 8, 'Compressed 64 bits', fontsize=12, fontweight='bold', ha='center')
    
    # 压缩数据结构
    comp_box = FancyBboxPatch((8, 5), 5, 2.5, boxstyle="round,pad=0.1",
                              facecolor='#e8f5e9', edgecolor='#4CAF50', linewidth=3)
    ax.add_patch(comp_box)
    
    data_blocks = [
        ('Color 0: 16 bits', 9.5, 6.8),
        ('Color 1: 16 bits', 9.5, 6.2),
        ('Indices: 32 bits', 9.5, 5.6),
    ]
    for label, x, y in data_blocks:
        ax.text(x, y, label, fontsize=10, fontfamily='monospace')
    
    # 压缩比
    ratio_box = FancyBboxPatch((4.5, 2), 5, 2, boxstyle="round,pad=0.1",
                               facecolor='#fff3e0', edgecolor='#ff9800', linewidth=2)
    ax.add_patch(ratio_box)
    ax.text(7, 3.3, 'Compression Ratio: 6:1', fontsize=14, fontweight='bold', ha='center')
    ax.text(7, 2.6, '384 bits → 64 bits (4×4 RGB block)', fontsize=11, ha='center', color='#666')
    
    # 格式对比
    ax.text(7, 1.2, 'BC1 (DXT1): 64 bits/block | BC7: 128 bits/block | ASTC: Variable', 
            fontsize=10, ha='center', color='#666')
    
    save_fig('block_compression.png')

if __name__ == '__main__':
    print("Generating high-quality technical diagrams...")
    print("=" * 50)
    
    create_rendering_pipeline()
    create_mesh_primitive()
    create_materials_pbr()
    create_pbr_textures()
    create_submesh_structure()
    create_instance_reuse()
    create_frustum_culling()
    create_bvh_culling()
    create_block_compression()
    
    print("=" * 50)
    print("All diagrams generated successfully!")

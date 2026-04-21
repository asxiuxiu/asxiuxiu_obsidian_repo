import math
from PIL import Image, ImageDraw, ImageFont
import os

# 画布
W, H = 1100, 750
img = Image.new('RGB', (W, H), (255, 255, 255))
draw = ImageDraw.Draw(img)

# 字体
font_path = 'C:/Windows/Fonts/msyh.ttc'
font_title = ImageFont.truetype(font_path, 24)
font_label = ImageFont.truetype(font_path, 14)
font_small = ImageFont.truetype(font_path, 12)
font_big = ImageFont.truetype(font_path, 28)

# 状态节点位置（中心点）和大小
node_w, node_h = 150, 70
nodes = {
    'I':  (220, 200),
    'S':  (600, 200),
    'E':  (220, 500),
    'M':  (600, 500),
}

colors = {
    'I': (200, 200, 200),
    'S': (180, 210, 255),
    'E': (180, 255, 200),
    'M': (255, 180, 180),
}

def draw_node(name, pos, color):
    x, y = pos
    x0, y0 = x - node_w//2, y - node_h//2
    x1, y1 = x + node_w//2, y + node_h//2
    r = 12
    # 圆角矩形
    draw.rounded_rectangle([x0, y0, x1, y1], radius=r, fill=color, outline=(50,50,50), width=2)
    # 文字
    bbox = draw.textbbox((0,0), name, font=font_title)
    tw, th = bbox[2]-bbox[0], bbox[3]-bbox[1]
    draw.text((x - tw//2, y - th//2 - 2), name, fill=(0,0,0), font=font_title)
    
    # 状态全称
    full = {'I':'Invalid', 'S':'Shared', 'E':'Exclusive', 'M':'Modified'}[name]
    bbox2 = draw.textbbox((0,0), full, font=font_small)
    tw2, th2 = bbox2[2]-bbox2[0], bbox2[3]-bbox2[1]
    draw.text((x - tw2//2, y + node_h//2 + 6), full, fill=(100,100,100), font=font_small)

# 画节点
for name, pos in nodes.items():
    draw_node(name, pos, colors[name])

# 画箭头函数
def arrow_points(x1, y1, x2, y2, size=10):
    dx, dy = x2 - x1, y2 - y1
    length = math.hypot(dx, dy)
    if length == 0:
        return []
    ux, uy = dx/length, dy/length
    # 终点往回退一点，避免插入节点内部
    back = 8
    ex, ey = x2 - ux*back, y2 - uy*back
    # 垂直向量
    vx, vy = -uy, ux
    p1 = (ex - ux*size + vx*size*0.5, ey - uy*size + vy*size*0.5)
    p2 = (ex - ux*size - vx*size*0.5, ey - uy*size - vy*size*0.5)
    return [(ex, ey), p1, p2]

def draw_arrow(draw, x1, y1, x2, y2, color=(60,60,60), width=2):
    draw.line([(x1,y1),(x2,y2)], fill=color, width=width)
    pts = arrow_points(x1,y1,x2,y2)
    if pts:
        draw.polygon(pts, fill=color)

def offset_point(from_pos, to_pos, w, h):
    """计算从from_pos出发，指向to_pos方向，在矩形边界上的交点"""
    x1, y1 = from_pos
    x2, y2 = to_pos
    dx, dy = x2 - x1, y2 - y1
    if dx == 0 and dy == 0:
        return from_pos
    hw, hh = w/2, h/2
    txs = []
    if dx > 0:
        txs.append(hw / dx)
    elif dx < 0:
        txs.append(hw / abs(dx))
    if dy > 0:
        txs.append(hh / dy)
    elif dy < 0:
        txs.append(hh / abs(dy))
    t = min(txs) if txs else 0
    return (x1 + dx*t, y1 + dy*t)

def draw_edge(draw, n1, n2, label, label_offset=(0,0), color=(60,60,60)):
    p1 = offset_point(nodes[n1], nodes[n2], node_w, node_h)
    p2 = offset_point(nodes[n2], nodes[n1], node_w, node_h)
    draw_arrow(draw, p1[0], p1[1], p2[0], p2[1], color=color)
    # 标签
    mx, my = (p1[0] + p2[0])/2 + label_offset[0], (p1[1] + p2[1])/2 + label_offset[1]
    lines = label.split('\n')
    total_h = len(lines) * 16
    for i, line in enumerate(lines):
        bbox = draw.textbbox((0,0), line, font=font_label)
        tw, th = bbox[2]-bbox[0], bbox[3]-bbox[1]
        # 白色背景垫片
        pad = 2
        draw.rectangle([mx-tw//2-pad, my-total_h//2+i*16-th//2-pad, mx+tw//2+pad, my-total_h//2+i*16+th//2+pad], fill=(255,255,255))
        draw.text((mx-tw//2, my-total_h//2+i*16-th//2), line, fill=(40,40,40), font=font_label)

# 定义边
edges = [
    ('I', 'S', '读未命中 (BusRd)\n其他核有此行 → 共享为S', (0, -35)),
    ('I', 'E', '读未命中 (BusRd)\n其他核无此行 → 独占E', (-65, -45)),
    ('E', 'M', '写命中\n无总线事务，直接变M', (0, 32)),
    ('S', 'M', '写命中 (BusUpgr)\n使其他核副本失效(I)，自己变M', (70, 22)),
    ('M', 'S', '其他核读 (BusRd)\n写回主存，降级为S', (70, -22)),
    ('E', 'S', '其他核读 (BusRd)\n自己降级为S', (35, -38)),
    ('S', 'I', '其他核写 (BusRdX)\n自己变为I', (0, 38)),
    ('E', 'I', '其他核写 (BusRdX)\n自己变为I', (-65, 45)),
    ('M', 'I', '其他核写 (BusRdX)\n写回主存，变为I', (55, 15)),
]

for n1, n2, label, off in edges:
    draw_edge(draw, n1, n2, label, off)

# 标题
draw.text((W//2 - 180, 30), 'MESI 缓存一致性协议状态转换图', fill=(0,0,0), font=font_big)

# 图例说明
legend_y = 650
lines = [
    '注：箭头表示状态转换触发条件，文字包含总线事务及对"其他核"的影响',
    'BusRd = 读请求广播    BusRdX = 读+独占请求    BusUpgr = 升级请求(使其他核失效)',
]
for i, line in enumerate(lines):
    bbox = draw.textbbox((0,0), line, font=font_small)
    tw = bbox[2]-bbox[0]
    draw.text((W//2 - tw//2, legend_y + i*22), line, fill=(100,100,100), font=font_small)

out_path = 'D:/obsidian_lib/Assets/操作系统/mesi-state-diagram.png'
img.save(out_path, 'PNG')
print('saved to', out_path)

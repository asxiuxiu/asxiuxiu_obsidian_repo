#pragma once

#include "math.h"
#include "image.h"
#include <algorithm>
#include <cmath>

namespace raster {


    void rasterizeDrawLineNaive(Image& image, const Vec2& p0, const Vec2& p1, const Color& color) {
        float k = (p1.y - p0.y) / (p1.x - p0.x);
        float b = p0.y - k * p0.x;

        int width = image.getWidth();
        int height = image.getHeight();

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                if (y == static_cast<int>(k * x + b)) {
                    image.setPixel(x, y, color);
                }
            }
        }
    }

    // Bresenham算法画线
    void rasterizeDrawLineBresenham(Image& image, const Vec2& p0, const Vec2& p1, const Color& color) {
        int x0 = static_cast<int>(std::round(p0.x));
        int y0 = static_cast<int>(std::round(p0.y));
        int x1 = static_cast<int>(std::round(p1.x));
        int y1 = static_cast<int>(std::round(p1.y));

        int dx = x1 - x0;
        int dy = y1 - y0;

        int y = y0;
        int e = 0; // 决策参数（误差累积）
        for (int i = x0; i <= x1; i++)
        {
            image.setPixel(i, y, color);
            e += 2 * dy;
            if (e >= dx)
            {
                y++;
                e -= 2 * dx;
            }
            
        }   
    }

    // 光栅化主函数 - 基础版本（遍历所有像素）
    void rasterizeTriangle(
        Image& image,                    // 输出图像
        const Triangle& tri,             // 要画的三角形
        const Color& color               // 填充颜色
    ) {
        int width = image.getWidth();
        int height = image.getHeight();

        // 遍历所有像素
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {

                // 取像素中心点坐标
                Vec2 pixelPos(x + 0.5f, y + 0.5f);

                // 判断是否在三角形内
                if (pointInTriangle(pixelPos, tri)) {
                    image.setPixel(x, y, color);
                }
            }
        }
    }

    // 光栅化 - 优化版本（包围盒）
    void rasterizeTriangleOptimized(Image& image, const Triangle& tri, const Color& color) {
        // 计算包围盒
        int minX = std::max(0, (int)std::min({ tri.p1.x, tri.p2.x, tri.p3.x }));
        int maxX = std::min(image.getWidth() - 1, (int)std::max({ tri.p1.x, tri.p2.x, tri.p3.x }) + 1);
        int minY = std::max(0, (int)std::min({ tri.p1.y, tri.p2.y, tri.p3.y }));
        int maxY = std::min(image.getHeight() - 1, (int)std::max({ tri.p1.y, tri.p2.y, tri.p3.y }) + 1);

        // 只遍历包围盒内的像素
        for (int y = minY; y <= maxY; y++) {
            for (int x = minX; x <= maxX; x++) {
                if (pointInTriangle(Vec2(x + 0.5f, y + 0.5f), tri)) {
                    image.setPixel(x, y, color);
                }
            }
        }
    }

} // namespace raster

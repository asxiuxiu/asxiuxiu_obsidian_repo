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

        int dx = std::abs(x1 - x0);
        int dy = std::abs(y1 - y0);

        bool steep = dy > dx;
        if (steep)
        {
            std::swap(x0, y0);
            std::swap(x1, y1);
            std::swap(dx, dy);
        }

        if (x0 > x1)
        {
            std::swap(x0, x1);
            std::swap(y0, y1);
        }

        int ySteep = y1 > y0 ? 1 : -1;

        int y = y0;
        int e = 0;

        for (int i = x0; i <= x1; i++)
        {
            if (steep)
            {
                image.setPixel(y, i, color);
            }
            else
            {
                image.setPixel(i, y, color);
            }
            e += 2 * dy;
            if (e >= dx)
            {
                y += ySteep;
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


    void rasterizeTriangleEdgeFunction(Image& image, const Vertex& v0, const Vertex& v1, const Vertex& v2) {
        const Vec2& p0 = v0.position;
        const Vec2& p1 = v1.position;
        const Vec2& p2 = v2.position;

        // 计算包围盒
        int minX = std::max(0, static_cast<int>(std::floor(std::min({ p0.x, p1.x, p2.x }))));
        int maxX = std::min(image.getWidth() - 1, static_cast<int>(std::ceil(std::max({ p0.x, p1.x, p2.x }))));
        int minY = std::max(0, static_cast<int>(std::floor(std::min({ p0.y, p1.y, p2.y }))));
        int maxY = std::min(image.getHeight() - 1, static_cast<int>(std::ceil(std::max({ p0.y, p1.y, p2.y }))));

        // 三角形有向面积 * 2
        float area2Triangle = cross(Vec2{ p2.x - p0.x, p2.y - p0.y }, Vec2{ p1.x - p0.x, p1.y - p0.y });
        if (area2Triangle == 0.f) {
            return;
        }

        float edge01StepX = (p1.y - p0.y);
        float edge12StepX = (p2.y - p1.y);
        float edge20StepX = (p0.y - p2.y);


        for (int y = minY; y <= maxY; y++)
        {
            Vec2 sample = { minX + 0.5f, y + 0.5f };

            float edge01 = cross(Vec2{ sample.x - p0.x, sample.y - p0.y }, Vec2{ p1.x - p0.x, p1.y - p0.y });
            float edge12 = cross(Vec2{ sample.x - p1.x, sample.y - p1.y }, Vec2{ p2.x - p1.x, p2.y - p1.y });
            float edge20 = cross(Vec2{ sample.x - p2.x, sample.y - p2.y }, Vec2{ p0.x - p2.x, p0.y - p2.y });

            for (int x = minX; x <= maxX; x++)
            {
                // 与三角形有向面积同号则在内部（兼容 CW / CCW 顶点顺序）
                if (edge01 * area2Triangle >= 0.f &&
                    edge12 * area2Triangle >= 0.f &&
                    edge20 * area2Triangle >= 0.f)
                {
                    // 重心坐标：w0 对 v0，w1 对 v1，w2 对 v2（与 edge01/edge12/edge20 对应）
                    float w0 = edge12 / area2Triangle;
                    float w1 = edge20 / area2Triangle;
                    float w2 = edge01 / area2Triangle;

                    auto lerpCh = [](float w0, uint8_t c0, float w1, uint8_t c1, float w2, uint8_t c2) -> uint8_t {
                        float v = std::round(
                            w0 * static_cast<float>(c0) + w1 * static_cast<float>(c1) + w2 * static_cast<float>(c2));
                        v = std::max(0.f, std::min(255.f, v));
                        return static_cast<uint8_t>(v);
                        };

                    Color finalColor(
                        lerpCh(w0, v0.color.r, w1, v1.color.r, w2, v2.color.r),
                        lerpCh(w0, v0.color.g, w1, v1.color.g, w2, v2.color.g),
                        lerpCh(w0, v0.color.b, w1, v1.color.b, w2, v2.color.b),
                        lerpCh(w0, v0.color.a, w1, v1.color.a, w2, v2.color.a)
                    );

                    image.setPixel(x, y, finalColor);
                }

                edge01 += edge01StepX;
                edge12 += edge12StepX;
                edge20 += edge20StepX;

            }

        }

    }
}

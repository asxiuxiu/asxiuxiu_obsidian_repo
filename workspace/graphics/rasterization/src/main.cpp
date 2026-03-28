#include "rasterizer.h"
#include <iostream>

using namespace raster;

namespace {

    // 与 Bresenham 对照共用同一点与颜色，便于逐图对比
    const Vec2 kLowP0(10, 20), kLowP1(90, 80);
    const Color kLowColor(255, 0, 0);

    const Vec2 kHighP0(20, 10), kHighP1(30, 90);
    const Color kHighColor(0, 0, 255);

    const Vec2 kFpe1P0(2, 5), kFpe1P1(98, 34);
    const Vec2 kFpe2P0(20, 5), kFpe2P1(30, 85);
    const Vec2 kFpe3P0(60, 60), kFpe3P1(80, 65);
    const Color kFpe1Color(200, 40, 40);
    const Color kFpe2Color(20, 200, 40);
    const Color kFpe3Color(40, 40, 255);

} // namespace

int main() {

    Image canvas0(100, 100);
    rasterizeDrawLineNaive(canvas0, kLowP0, kLowP1, kLowColor);
    canvas0.save("line_naive.ppm");
    std::cout << "已生成 line_naive.ppm" << std::endl;


    Image canvas1(100, 100);
    rasterizeDrawLineNaive(canvas1, kHighP0, kHighP1, kHighColor);
    canvas1.save("line_k_gt_1.ppm");
    std::cout << "已生成 line_k_gt_1.ppm" << std::endl;


    Image canvas_fpe(100, 100);
    rasterizeDrawLineNaive(canvas_fpe, kFpe1P0, kFpe1P1, kFpe1Color);
    rasterizeDrawLineNaive(canvas_fpe, kFpe2P0, kFpe2P1, kFpe2Color);
    rasterizeDrawLineNaive(canvas_fpe, kFpe3P0, kFpe3P1, kFpe3Color);
    canvas_fpe.save("line_floating_point_error.ppm");
    std::cout << "已生成 line_floating_point_error.ppm（与 line_bresenham_fpe.ppm 同数据对照）" << std::endl;


    Image canvas_bres_low(100, 100);
    rasterizeDrawLineBresenham(canvas_bres_low, kLowP0, kLowP1, kLowColor);
    canvas_bres_low.save("line_bresenham_low.ppm");
    std::cout << "已生成 line_bresenham_low.ppm（与 line_naive.ppm 同数据对照）" << std::endl;

    Image canvas_bres_high(100, 100);
    rasterizeDrawLineBresenham(canvas_bres_high, kHighP0, kHighP1, kHighColor);
    canvas_bres_high.save("line_bresenham_high.ppm");
    std::cout << "已生成 line_bresenham_high.ppm（与 line_k_gt_1.ppm 同数据对照）" << std::endl;

    Image canvas_bres_fpe(100, 100);
    rasterizeDrawLineBresenham(canvas_bres_fpe, kFpe1P0, kFpe1P1, kFpe1Color);
    rasterizeDrawLineBresenham(canvas_bres_fpe, kFpe2P0, kFpe2P1, kFpe2Color);
    rasterizeDrawLineBresenham(canvas_bres_fpe, kFpe3P0, kFpe3P1, kFpe3Color);
    canvas_bres_fpe.save("line_bresenham_fpe.ppm");
    std::cout << "已生成 line_bresenham_fpe.ppm（与 line_floating_point_error.ppm 同数据对照）" << std::endl;


    // 创建 100x100 的图像
    Image canvas(100, 100);

    // 定义一个红色三角形（只需三个顶点！）
    Triangle tri(
        Vec2(50, 10),   // 顶点1：顶部
        Vec2(10, 90),   // 顶点2：左下
        Vec2(90, 90)    // 顶点3：右下
    );

    // 光栅化 —— 自动填充三角形内的所有像素
    rasterizeTriangle(canvas, tri, Color(255, 0, 0));

    // 保存结果
    canvas.save("red_triangle.ppm");

    std::cout << "已生成 red_triangle.ppm" << std::endl;

    // 使用优化版本再画一个
    Image canvas2(100, 100);
    rasterizeTriangleOptimized(canvas2, tri, Color(255, 0, 0));
    canvas2.save("red_triangle_optimized.ppm");

    std::cout << "已生成 red_triangle_optimized.ppm" << std::endl;

    return 0;
}


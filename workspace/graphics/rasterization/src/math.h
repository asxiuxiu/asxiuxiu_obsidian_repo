#pragma once

#include <cstdint>
#include <algorithm>

namespace raster {

// 二维点/向量
struct Vec2 {
    float x, y;
    Vec2(float xVal = 0, float yVal = 0) : x(xVal), y(yVal) {}
    
    Vec2 operator-(const Vec2& other) const {
        return Vec2(x - other.x, y - other.y);
    }
};

// 三角形 = 三个顶点
struct Triangle {
    Vec2 p1, p2, p3;
    Triangle(const Vec2& a, const Vec2& b, const Vec2& c) 
        : p1(a), p2(b), p3(c) {}
};

// 颜色（RGBA）
struct Color {
    uint8_t r, g, b, a;
    Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) 
        : r(r), g(g), b(b), a(a) {}
};

// 2D 叉乘：a × b = ax * by - ay * bx
// 结果 > 0：b 在 a 的左侧
// 结果 < 0：b 在 a 的右侧
// 结果 = 0：b 与 a 共线
inline float cross(const Vec2& a, const Vec2& b) {
    return a.x * b.y - a.y * b.x;
}

// 判断点是否在三角形内
inline bool pointInTriangle(const Vec2& p, const Triangle& tri) {
    // 三角形的三条边
    Vec2 e1 = tri.p2 - tri.p1;  // 边 P1→P2
    Vec2 e2 = tri.p3 - tri.p2;  // 边 P2→P3
    Vec2 e3 = tri.p1 - tri.p3;  // 边 P3→P1
    
    // 从顶点指向待判断点的向量
    Vec2 v1 = p - tri.p1;
    Vec2 v2 = p - tri.p2;
    Vec2 v3 = p - tri.p3;
    
    // 三次叉乘
    float c1 = cross(e1, v1);
    float c2 = cross(e2, v2);
    float c3 = cross(e3, v3);
    
    // 符号相同则在内部（全正或全负）
    bool hasNeg = (c1 < 0) || (c2 < 0) || (c3 < 0);
    bool hasPos = (c1 > 0) || (c2 > 0) || (c3 > 0);
    
    return !(hasNeg && hasPos);
}

} // namespace raster

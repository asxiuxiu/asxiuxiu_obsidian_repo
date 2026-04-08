#include <iostream>
#include <cmath>
#include <vector>
#include <fstream>
#include <algorithm>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>

#include "obj_loader.hpp"

int main() {
    obj::Model model;
    // 尝试从当前工作目录或 build 子目录加载
    if (!model.load("assets/cube.obj") && !model.load("../assets/cube.obj")) {
        std::cerr << "Failed to load model!" << std::endl;
        return -1;
    }

    std::cout << "=== OBJ Load Result ===" << std::endl;
    std::cout << "Mesh count: " << model.meshes().size() << std::endl;

    size_t totalVertices = 0;
    size_t totalIndices = 0;

    for (const auto& mesh : model.meshes()) {
        std::cout << "-----------------------" << std::endl;
        std::cout << "Mesh: " << mesh.name << std::endl;
        std::cout << "  Vertices: " << mesh.vertices.size() << std::endl;
        std::cout << "  Indices:  " << mesh.indices.size() << std::endl;
        std::cout << "  Triangles: " << mesh.indices.size() / 3 << std::endl;

        totalVertices += mesh.vertices.size();
        totalIndices += mesh.indices.size();
    }

    // =========================================================================
    // Step 1: Model Transformation (Local Space -> World Space)
    // =========================================================================
    std::cout << "-----------------------" << std::endl;
    std::cout << "=== Model Transformation ===" << std::endl;

    // 世界坐标系定义：右手坐标系
    // - 原点 (0, 0, 0)
    // - X轴向右
    // - Y轴向上
    // - Z轴朝向观察者（从屏幕向外）
    glm::vec3 worldOrigin(0.0f, 0.0f, 0.0f);
    glm::vec3 worldRight(1.0f, 0.0f, 0.0f);
    glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    glm::vec3 worldForward(0.0f, 0.0f, 1.0f);

    std::cout << "World Coordinate System (Right-Handed):" << std::endl;
    std::cout << "  Origin:   (" << worldOrigin.x << ", " << worldOrigin.y << ", " << worldOrigin.z << ")" << std::endl;
    std::cout << "  Right(X): (" << worldRight.x << ", " << worldRight.y << ", " << worldRight.z << ")" << std::endl;
    std::cout << "  Up(Y):    (" << worldUp.x << ", " << worldUp.y << ", " << worldUp.z << ")" << std::endl;
    std::cout << "  Forward(Z): (" << worldForward.x << ", " << worldForward.y << ", " << worldForward.z << ")" << std::endl;

    // 模型在世界坐标系中的位置和旋转
    glm::vec3 modelPosition(2.0f, 0.5f, -3.0f);  // 世界坐标中的位置
    glm::vec3 modelRotationAxis(0.0f, 1.0f, 0.0f); // 绕 Y 轴旋转
    float modelRotationAngle = glm::radians(45.0f); // 旋转 45 度
    glm::vec3 modelScale(1.0f, 1.0f, 1.0f);        // 均匀缩放

    std::cout << "Model in World Space:" << std::endl;
    std::cout << "  Position:  (" << modelPosition.x << ", " << modelPosition.y << ", " << modelPosition.z << ")" << std::endl;
    std::cout << "  Rotation:  " << glm::degrees(modelRotationAngle) << " degrees around Y-axis" << std::endl;
    std::cout << "  Scale:     (" << modelScale.x << ", " << modelScale.y << ", " << modelScale.z << ")" << std::endl;

    // 构建 Model 变换矩阵: M = T * R * S
    glm::mat4 modelMatrix = glm::mat4(1.0f);
    modelMatrix = glm::translate(modelMatrix, modelPosition);
    modelMatrix = glm::rotate(modelMatrix, modelRotationAngle, modelRotationAxis);
    modelMatrix = glm::scale(modelMatrix, modelScale);

    std::cout << "Model Matrix:" << std::endl;
    for (int row = 0; row < 4; ++row) {
        std::cout << "  | ";
        for (int col = 0; col < 4; ++col) {
            std::cout << modelMatrix[col][row] << " ";
        }
        std::cout << "|" << std::endl;
    }

    // =========================================================================
    // Step 2: View Transformation (World Space -> View Space / Camera Space)
    // =========================================================================
    std::cout << "-----------------------" << std::endl;
    std::cout << "=== View Transformation ===" << std::endl;

    // 标准相机配置：变换后相机位于原点，Up 朝 +Y，朝向 -Z
    // 为了让相机能看到模型，我们将相机放置在模型的斜上方
    glm::vec3 cameraPosition(2.0f, 5.0f, 5.0f);   // 相机在世界空间的位置
    glm::vec3 cameraTarget(modelPosition);          // 相机看向模型中心
    glm::vec3 cameraUp(0.5f, 1.0f, 0.0f);           // 相机的上方向在世界坐标系下是倾斜的

    std::cout << "Camera Configuration (World Space):" << std::endl;
    std::cout << "  Position:  (" << cameraPosition.x << ", " << cameraPosition.y << ", " << cameraPosition.z << ")" << std::endl;
    std::cout << "  Target:    (" << cameraTarget.x << ", " << cameraTarget.y << ", " << cameraTarget.z << ")" << std::endl;
    std::cout << "  Up:        (" << cameraUp.x << ", " << cameraUp.y << ", " << cameraUp.z << ")" << std::endl;
    std::cout << "  Standard View Space: camera at origin, looking down -Z, Y is up" << std::endl;

    // 构建 View 变换矩阵
    glm::mat4 viewMatrix = glm::lookAt(cameraPosition, cameraTarget, cameraUp);

    std::cout << "View Matrix:" << std::endl;
    for (int row = 0; row < 4; ++row) {
        std::cout << "  | ";
        for (int col = 0; col < 4; ++col) {
            std::cout << viewMatrix[col][row] << " ";
        }
        std::cout << "|" << std::endl;
    }

    // =========================================================================
    // Step 3: Projection Transformation (View Space -> Clip Space)
    // =========================================================================
    std::cout << "-----------------------" << std::endl;
    std::cout << "=== Projection Transformation ===" << std::endl;

    // 定义相机投影参数
    float fov = glm::radians(60.0f);   // 垂直视野角度 (Field of View)
    float aspect = 16.0f / 9.0f;         // 宽高比
    float nearPlane = 0.1f;              // 近裁剪面
    float farPlane = 100.0f;             // 远裁剪面

    std::cout << "Projection Parameters:" << std::endl;
    std::cout << "  FOV:       " << glm::degrees(fov) << " degrees" << std::endl;
    std::cout << "  Aspect:    " << aspect << std::endl;
    std::cout << "  Near:      " << nearPlane << std::endl;
    std::cout << "  Far:       " << farPlane << std::endl;

    // 构建透视投影矩阵
    glm::mat4 projectionMatrix = glm::perspective(fov, aspect, nearPlane, farPlane);

    std::cout << "Projection Matrix:" << std::endl;
    for (int row = 0; row < 4; ++row) {
        std::cout << "  | ";
        for (int col = 0; col < 4; ++col) {
            std::cout << projectionMatrix[col][row] << " ";
        }
        std::cout << "|" << std::endl;
    }

    // =========================================================================
    // Step 4: Viewport Transformation (NDC -> Screen Space)
    // =========================================================================
    std::cout << "-----------------------" << std::endl;
    std::cout << "=== Viewport Transformation ===" << std::endl;

    const int WIDTH = 800;
    const int HEIGHT = 450;
    std::cout << "Screen Resolution: " << WIDTH << " x " << HEIGHT << std::endl;

    // 打印第一个 mesh 的前几个顶点作为示例
    if (!model.meshes().empty()) {
        const auto& mesh = model.meshes().front();
        std::cout << "-----------------------" << std::endl;
        std::cout << "First mesh first 3 vertices transformations:" << std::endl;
        for (size_t i = 0; i < std::min(size_t(3), mesh.vertices.size()); ++i) {
            const auto& v = mesh.vertices[i];
            std::cout << "  v[" << i << "] Local pos=("
                << v.position.x << ", " << v.position.y << ", " << v.position.z << ")"
                << std::endl;

            // 将局部坐标变换到世界坐标
            glm::vec4 worldPos = modelMatrix * glm::vec4(v.position, 1.0f);
            std::cout << "      -> World pos=("
                << worldPos.x << ", " << worldPos.y << ", " << worldPos.z << ")"
                << std::endl;

            // 将世界坐标变换到视图坐标（相机空间）
            glm::vec4 viewPos = viewMatrix * worldPos;
            std::cout << "      -> View pos=("
                << viewPos.x << ", " << viewPos.y << ", " << viewPos.z << ")"
                << std::endl;

            // 将视图坐标变换到裁剪空间（Clip Space）
            glm::vec4 clipPos = projectionMatrix * viewPos;
            std::cout << "      -> Clip pos=("
                << clipPos.x << ", " << clipPos.y << ", " << clipPos.z << ", " << clipPos.w << ")"
                << std::endl;

            // 透视除法：Clip Space -> NDC (Normalized Device Coordinates)
            // NDC 的范围是 [-1, 1]
            if (std::abs(clipPos.w) > 1e-6f) {
                glm::vec3 ndcPos = glm::vec3(clipPos) / clipPos.w;
                std::cout << "      -> NDC pos=("
                    << ndcPos.x << ", " << ndcPos.y << ", " << ndcPos.z << ")"
                    << std::endl;

                // NDC -> Screen Space
                float screenX = (ndcPos.x + 1.0f) * 0.5f * static_cast<float>(WIDTH);
                float screenY = (1.0f - ndcPos.y) * 0.5f * static_cast<float>(HEIGHT);

                int px = static_cast<int>(std::floor(screenX));
                int py = static_cast<int>(std::floor(screenY));
                px = std::max(0, std::min(px, WIDTH - 1));
                py = std::max(0, std::min(py, HEIGHT - 1));

                std::cout << "      -> Screen pos=("
                    << screenX << ", " << screenY << ")"
                    << "  Pixel=(" << px << ", " << py << ")"
                    << std::endl;
            }
            else {
                std::cout << "      -> NDC pos=(w is zero, cannot divide)" << std::endl;
            }
        }
    }

    // 遍历所有 mesh 的所有顶点，打印屏幕坐标统计
    std::cout << "-----------------------" << std::endl;
    std::cout << "All vertices screen space summary:" << std::endl;
    size_t totalVerticesProcessed = 0;
    size_t visibleVertices = 0;
    for (const auto& mesh : model.meshes()) {
        for (const auto& v : mesh.vertices) {
            glm::vec4 worldPos = modelMatrix * glm::vec4(v.position, 1.0f);
            glm::vec4 viewPos = viewMatrix * worldPos;
            glm::vec4 clipPos = projectionMatrix * viewPos;

            if (std::abs(clipPos.w) > 1e-6f) {
                glm::vec3 ndcPos = glm::vec3(clipPos) / clipPos.w;
                float screenX = (ndcPos.x + 1.0f) * 0.5f * static_cast<float>(WIDTH);
                float screenY = (1.0f - ndcPos.y) * 0.5f * static_cast<float>(HEIGHT);

                bool insideNDC = (ndcPos.x >= -1.0f && ndcPos.x <= 1.0f &&
                                  ndcPos.y >= -1.0f && ndcPos.y <= 1.0f &&
                                  ndcPos.z >= -1.0f && ndcPos.z <= 1.0f);
                if (insideNDC) {
                    ++visibleVertices;
                }
            }
            ++totalVerticesProcessed;
        }
    }
    std::cout << "  Total vertices: " << totalVerticesProcessed << std::endl;
    std::cout << "  Vertices inside NDC cube (potentially visible): " << visibleVertices << std::endl;

    // =========================================================================
    // Step 5: Rasterization (Triangle Filling with Z-Buffer)
    // =========================================================================
    std::cout << "-----------------------" << std::endl;
    std::cout << "=== Rasterization ===" << std::endl;

    // 自动缩放模型，使其在世界空间中尺寸合理
    glm::vec3 modelMin, modelMax;
    model.computeBounds(modelMin, modelMax);
    glm::vec3 modelCenter = (modelMin + modelMax) * 0.5f;
    float modelExtent = glm::max(glm::max(modelMax.x - modelMin.x, modelMax.y - modelMin.y), modelMax.z - modelMin.z);
    float targetSize = 2.0f; // 目标最大尺寸为 2 个世界单位
    float autoScale = (modelExtent > 1e-6f) ? (targetSize / modelExtent) : 1.0f;

    std::cout << "Model bounds: min=(" << modelMin.x << ", " << modelMin.y << ", " << modelMin.z
              << ") max=(" << modelMax.x << ", " << modelMax.y << ", " << modelMax.z << ")" << std::endl;
    std::cout << "Auto-scale: " << autoScale << " (target world size=" << targetSize << ")" << std::endl;

    // 重建带自动缩放的 Model 矩阵: T(pos) * R * S * T(-center)
    glm::mat4 modelMatrixScaled = glm::mat4(1.0f);
    modelMatrixScaled = glm::translate(modelMatrixScaled, modelPosition);
    modelMatrixScaled = glm::rotate(modelMatrixScaled, modelRotationAngle, modelRotationAxis);
    modelMatrixScaled = glm::scale(modelMatrixScaled, modelScale * autoScale);
    modelMatrixScaled = glm::translate(modelMatrixScaled, -modelCenter);

    // Framebuffer & Z-Buffer
    std::vector<uint8_t> colorBuffer(WIDTH * HEIGHT * 3, 0);
    std::vector<float> depthBuffer(WIDTH * HEIGHT, 1.0f); // 1.0 = 最远

    auto setPixel = [&](int x, int y, const glm::vec3& color) {
        if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return;
        size_t idx = static_cast<size_t>((y * WIDTH + x) * 3);
        colorBuffer[idx + 0] = static_cast<uint8_t>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f);
        colorBuffer[idx + 1] = static_cast<uint8_t>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f);
        colorBuffer[idx + 2] = static_cast<uint8_t>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f);
    };

    auto writePPM = [&](const std::string& path) {
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Failed to open " << path << " for writing!" << std::endl;
            return;
        }
        file << "P6\n" << WIDTH << " " << HEIGHT << "\n255\n";
        file.write(reinterpret_cast<const char*>(colorBuffer.data()), colorBuffer.size());
    };

    // MVP 矩阵
    glm::mat4 mvp = projectionMatrix * viewMatrix * modelMatrixScaled;

    int trianglesDrawn = 0;

    for (const auto& mesh : model.meshes()) {
        const size_t triCount = mesh.indices.size() / 3;
        for (size_t t = 0; t < triCount; ++t) {
            const obj::Vertex& v0 = mesh.vertices[mesh.indices[t * 3 + 0]];
            const obj::Vertex& v1 = mesh.vertices[mesh.indices[t * 3 + 1]];
            const obj::Vertex& v2 = mesh.vertices[mesh.indices[t * 3 + 2]];

            // Local -> Clip
            glm::vec4 c0 = mvp * glm::vec4(v0.position, 1.0f);
            glm::vec4 c1 = mvp * glm::vec4(v1.position, 1.0f);
            glm::vec4 c2 = mvp * glm::vec4(v2.position, 1.0f);

            // 简单裁剪：w <= 0 表示在相机后方或平面上，跳过
            if (c0.w <= 0.0f || c1.w <= 0.0f || c2.w <= 0.0f) continue;

            // 透视除法 -> NDC
            glm::vec3 ndc0 = glm::vec3(c0) / c0.w;
            glm::vec3 ndc1 = glm::vec3(c1) / c1.w;
            glm::vec3 ndc2 = glm::vec3(c2) / c2.w;

            // NDC -> Screen Space (Y 翻转，原点左上角)
            auto toScreen = [&](const glm::vec3& ndc) -> glm::vec2 {
                return glm::vec2(
                    (ndc.x + 1.0f) * 0.5f * static_cast<float>(WIDTH),
                    (1.0f - ndc.y) * 0.5f * static_cast<float>(HEIGHT)
                );
            };

            glm::vec2 s0 = toScreen(ndc0);
            glm::vec2 s1 = toScreen(ndc1);
            glm::vec2 s2 = toScreen(ndc2);

            // AABB (裁剪到屏幕)
            int minX = static_cast<int>(std::floor(glm::min(glm::min(s0.x, s1.x), s2.x)));
            int maxX = static_cast<int>(std::ceil (glm::max(glm::max(s0.x, s1.x), s2.x)));
            int minY = static_cast<int>(std::floor(glm::min(glm::min(s0.y, s1.y), s2.y)));
            int maxY = static_cast<int>(std::ceil (glm::max(glm::max(s0.y, s1.y), s2.y)));

            minX = std::max(minX, 0);
            maxX = std::min(maxX, WIDTH - 1);
            minY = std::max(minY, 0);
            maxY = std::min(maxY, HEIGHT - 1);
            if (minX > maxX || minY > maxY) continue;

            // Barycentric 分母 (三角形面积的两倍)
            float denom = (s1.y - s2.y) * (s0.x - s2.x) + (s2.x - s1.x) * (s0.y - s2.y);
            if (std::abs(denom) < 1e-6f) continue; // 退化三角形

            // 光栅化 AABB 内每个像素
            for (int py = minY; py <= maxY; ++py) {
                for (int px = minX; px <= maxX; ++px) {
                    float px_f = static_cast<float>(px) + 0.5f;
                    float py_f = static_cast<float>(py) + 0.5f;

                    float w0 = ((s1.y - s2.y) * (px_f - s2.x) + (s2.x - s1.x) * (py_f - s2.y)) / denom;
                    float w1 = ((s2.y - s0.y) * (px_f - s2.x) + (s0.x - s2.x) * (py_f - s2.y)) / denom;
                    float w2 = 1.0f - w0 - w1;

                    if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;

                    // 深度插值 (NDC z 在 [-1,1]，映射到 [0,1])
                    float z = w0 * ndc0.z + w1 * ndc1.z + w2 * ndc2.z;
                    float depth = z * 0.5f + 0.5f;

                    size_t zIdx = static_cast<size_t>(py * WIDTH + px);
                    if (depth < depthBuffer[zIdx]) {
                        depthBuffer[zIdx] = depth;

                        // 颜色：插值法线并映射到 RGB
                        glm::vec3 normal = glm::normalize(v0.normal * w0 + v1.normal * w1 + v2.normal * w2);
                        glm::vec3 color = normal * 0.5f + 0.5f;
                        setPixel(px, py, color);
                    }
                }
            }
            ++trianglesDrawn;
        }
    }

    writePPM("../output_rasterized.ppm");
    std::cout << "Rasterization complete." << std::endl;
    std::cout << "Triangles rasterized: " << trianglesDrawn << std::endl;
    std::cout << "Output: output_rasterized.ppm (" << WIDTH << "x" << HEIGHT << ")" << std::endl;

    return 0;
}

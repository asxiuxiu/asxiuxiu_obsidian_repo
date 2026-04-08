#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace obj {

/**
 * OBJ 顶点数据
 * 包含位置、法线、纹理坐标
 */
struct Vertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 0.0f, 1.0f};
    glm::vec2 texcoord{0.0f};
};

/**
 * MTL 材质数据
 */
struct Material {
    std::string name;
    glm::vec3 Ka{1.0f};
    glm::vec3 Kd{0.64f};
    glm::vec3 Ks{0.5f};
    float Ns{96.078431f};
};

/**
 * 一个 Mesh 对应 OBJ 中的一个 object (o 关键字)
 */
struct Mesh {
    std::string name;
    std::string materialName;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

/**
 * OBJ 模型解析器
 * 只解析几何数据，不处理 .mtl 材质文件
 */
class Model {
public:
    Model() = default;

    /**
     * 从文件加载 OBJ 模型
     * @param filepath OBJ 文件路径
     * @return 是否加载成功
     */
    bool load(const std::string& filepath);

    /**
     * 清空已加载的数据
     */
    void clear();

    /**
     * 获取所有 Mesh
     */
    const std::vector<Mesh>& meshes() const { return meshes_; }

    /**
     * 获取所有材质
     */
    const std::vector<Material>& materials() const { return materials_; }

    /**
     * 根据名称查找材质
     */
    const Material* findMaterial(const std::string& name) const;

    /**
     * 计算整个模型的 AABB 包围盒
     */
    void computeBounds(glm::vec3& outMin, glm::vec3& outMax) const;

private:
    std::vector<Mesh> meshes_;
    std::vector<Material> materials_;

};

} // namespace obj

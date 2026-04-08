#include "obj_loader.hpp"

#include <fstream>
#include <sstream>
#include <iostream>
#include <unordered_map>
#include <tuple>

namespace obj {

// 面的单个顶点索引组合
struct FaceIndex {
    int v;   // position index (1-based in OBJ, 0 = not provided)
    int vt;  // texcoord index
    int vn;  // normal index

    bool operator==(const FaceIndex& other) const {
        return v == other.v && vt == other.vt && vn == other.vn;
    }
};

} // namespace obj

// 为 FaceIndex 提供哈希，用于顶点去重
namespace std {
template <>
struct hash<obj::FaceIndex> {
    size_t operator()(const obj::FaceIndex& idx) const noexcept {
        size_t h1 = std::hash<int>{}(idx.v);
        size_t h2 = std::hash<int>{}(idx.vt);
        size_t h3 = std::hash<int>{}(idx.vn);
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};
} // namespace std

namespace obj {

// 解析面的一小段，如 "12/34/56" 或 "12//56" 或 "12"
static FaceIndex parseFaceVertex(const std::string& token) {
    FaceIndex idx{0, 0, 0};
    size_t firstSlash = token.find('/');
    
    if (firstSlash == std::string::npos) {
        // v
        idx.v = std::stoi(token);
    } else {
        idx.v = std::stoi(token.substr(0, firstSlash));
        size_t secondSlash = token.find('/', firstSlash + 1);
        
        if (secondSlash == std::string::npos) {
            // v/vt
            std::string vtStr = token.substr(firstSlash + 1);
            if (!vtStr.empty()) idx.vt = std::stoi(vtStr);
        } else {
            // v/vt/vn 或 v//vn
            std::string vtStr = token.substr(firstSlash + 1, secondSlash - firstSlash - 1);
            if (!vtStr.empty()) idx.vt = std::stoi(vtStr);
            std::string vnStr = token.substr(secondSlash + 1);
            if (!vnStr.empty()) idx.vn = std::stoi(vnStr);
        }
    }
    return idx;
}

// 将可能为负的索引转换为有效的 0-based 索引
static int resolveIndex(int idx, size_t count) {
    if (idx > 0) return idx - 1;
    if (idx < 0) return static_cast<int>(count) + idx; // -1 表示最后一个元素
    return -1; // 0 表示未提供
}

// 解析 MTL 文件
static bool parseMTL(const std::string& filepath, std::vector<obj::Material>& outMaterials) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[MTL] Failed to open file: " << filepath << std::endl;
        return false;
    }

    obj::Material* current = nullptr;
    std::string line;
    while (std::getline(file, line)) {
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        if (line[start] == '#') continue;

        std::istringstream iss(line.substr(start));
        std::string keyword;
        iss >> keyword;

        if (keyword == "newmtl") {
            std::string name;
            std::getline(iss, name);
            size_t nameStart = name.find_first_not_of(" \t\r\n");
            if (nameStart != std::string::npos) {
                outMaterials.push_back(obj::Material{name.substr(nameStart)});
                current = &outMaterials.back();
            }
        }
        else if (keyword == "Ka" && current) {
            iss >> current->Ka.r >> current->Ka.g >> current->Ka.b;
        }
        else if (keyword == "Kd" && current) {
            iss >> current->Kd.r >> current->Kd.g >> current->Kd.b;
        }
        else if (keyword == "Ks" && current) {
            iss >> current->Ks.r >> current->Ks.g >> current->Ks.b;
        }
        else if (keyword == "Ns" && current) {
            iss >> current->Ns;
        }
    }

    file.close();
    return !outMaterials.empty();
}

bool Model::load(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[OBJ] Failed to open file: " << filepath << std::endl;
        return false;
    }

    clear();

    // 全局临时数据
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> texcoords;

    // 当前正在构建的 mesh
    Mesh currentMesh;
    bool hasCurrentMesh = false;

    // 顶点去重映射：FaceIndex -> 实际顶点在 currentMesh.vertices 中的索引
    std::unordered_map<FaceIndex, uint32_t> vertexCache;

    std::string line;
    while (std::getline(file, line)) {
        // 去掉行首空白
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        if (line[start] == '#') continue; // 注释

        std::istringstream iss(line.substr(start));
        std::string keyword;
        iss >> keyword;

        if (keyword == "v") {
            glm::vec3 p;
            iss >> p.x >> p.y >> p.z;
            positions.push_back(p);
        }
        else if (keyword == "vn") {
            glm::vec3 n;
            iss >> n.x >> n.y >> n.z;
            normals.push_back(n);
        }
        else if (keyword == "vt") {
            glm::vec2 t;
            iss >> t.x >> t.y;
            texcoords.push_back(t);
        }
        else if (keyword == "o" || keyword == "g") {
            // 保存上一个 mesh
            if (hasCurrentMesh) {
                meshes_.push_back(std::move(currentMesh));
                currentMesh = Mesh();
                vertexCache.clear();
                hasCurrentMesh = false;
            }
            // 读取 mesh 名称
            std::string name;
            std::getline(iss, name);
            size_t nameStart = name.find_first_not_of(" \t\r\n");
            if (nameStart != std::string::npos) {
                currentMesh.name = name.substr(nameStart);
            } else {
                currentMesh.name = "unnamed";
            }
            hasCurrentMesh = true;
        }
        else if (keyword == "usemtl") {
            std::string name;
            std::getline(iss, name);
            size_t nameStart = name.find_first_not_of(" \t\r\n");
            if (nameStart != std::string::npos) {
                currentMesh.materialName = name.substr(nameStart);
            }
        }
        else if (keyword == "f") {
            if (!hasCurrentMesh) {
                currentMesh.name = "default";
                hasCurrentMesh = true;
            }

            std::vector<FaceIndex> faceIndices;
            std::string token;
            while (iss >> token) {
                faceIndices.push_back(parseFaceVertex(token));
            }

            if (faceIndices.size() < 3) {
                std::cerr << "[OBJ] Face with less than 3 vertices ignored." << std::endl;
                continue;
            }

            // 扇形三角化 (0, i-1, i)
            for (size_t i = 2; i < faceIndices.size(); ++i) {
                FaceIndex tri[3] = {faceIndices[0], faceIndices[i - 1], faceIndices[i]};
                
                for (int j = 0; j < 3; ++j) {
                    const FaceIndex& fi = tri[j];
                    auto it = vertexCache.find(fi);
                    if (it != vertexCache.end()) {
                        currentMesh.indices.push_back(it->second);
                    } else {
                        Vertex v;
                        int pi = resolveIndex(fi.v, positions.size());
                        int ni = resolveIndex(fi.vn, normals.size());
                        int ti = resolveIndex(fi.vt, texcoords.size());

                        if (pi >= 0 && pi < static_cast<int>(positions.size())) {
                            v.position = positions[pi];
                        }
                        if (ni >= 0 && ni < static_cast<int>(normals.size())) {
                            v.normal = normals[ni];
                        }
                        if (ti >= 0 && ti < static_cast<int>(texcoords.size())) {
                            v.texcoord = texcoords[ti];
                        }

                        uint32_t newIdx = static_cast<uint32_t>(currentMesh.vertices.size());
                        currentMesh.vertices.push_back(v);
                        vertexCache[fi] = newIdx;
                        currentMesh.indices.push_back(newIdx);
                    }
                }
            }
        }
        else if (keyword == "mtllib") {
            std::string mtlFilename;
            std::getline(iss, mtlFilename);
            size_t nameStart = mtlFilename.find_first_not_of(" \t\r\n");
            if (nameStart != std::string::npos) {
                mtlFilename = mtlFilename.substr(nameStart);
                size_t lastSlash = filepath.find_last_of("/\\");
                std::string mtlPath = (lastSlash != std::string::npos)
                    ? filepath.substr(0, lastSlash + 1) + mtlFilename
                    : mtlFilename;
                parseMTL(mtlPath, materials_);
            }
        }
        // 忽略 s 等其他关键字
    }

    if (hasCurrentMesh) {
        meshes_.push_back(std::move(currentMesh));
    }

    file.close();
    return !meshes_.empty();
}

const Material* Model::findMaterial(const std::string& name) const {
    for (const auto& mat : materials_) {
        if (mat.name == name) return &mat;
    }
    return nullptr;
}

void Model::clear() {
    meshes_.clear();
    materials_.clear();
}

void Model::computeBounds(glm::vec3& outMin, glm::vec3& outMax) const {
    outMin = glm::vec3(std::numeric_limits<float>::max());
    outMax = glm::vec3(std::numeric_limits<float>::lowest());

    for (const auto& mesh : meshes_) {
        for (const auto& v : mesh.vertices) {
            outMin = glm::min(outMin, v.position);
            outMax = glm::max(outMax, v.position);
        }
    }
}

} // namespace obj

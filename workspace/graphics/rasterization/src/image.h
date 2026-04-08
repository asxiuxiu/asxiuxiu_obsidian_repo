#pragma once

#include <vector>
#include <string>
#include <fstream>
#include <stdexcept>
#include "math.h"

namespace raster {

// 图像类 - 输出 PPM 格式
class Image {
public:
    Image(int width, int height) 
        : width_(width), height_(height), data_(width * height, Color(0, 0, 0)) {}
    
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    
    void setPixel(int x, int y, const Color& color) {
        if (x < 0 || x >= width_ || y < 0 || y >= height_) return;
        data_[y * width_ + x] = color;
    }
    
    // 保存为 PPM 格式 (P3 - ASCII)
    void save(const std::string& filename) {
        std::ofstream file(filename);
        if (!file.is_open()) {
            throw std::runtime_error("无法创建文件: " + filename);
        }
        
        // PPM 文件头
        file << "P3\n";                           // 格式: P3 = ASCII PPM
        file << width_ << " " << height_ << "\n"; // 宽高
        file << "255\n";                          // 最大颜色值
        
        // 像素数据 (从左上到右下，Y轴向下)
        for (int y = 0; y < height_; y++) {
            for (int x = 0; x < width_; x++) {
                const Color& c = data_[y * width_ + x];
                file << (int)c.r << " " << (int)c.g << " " << (int)c.b << " ";
            }
            file << "\n";
        }
        
        file.close();
    }

private:
    int width_, height_;
    std::vector<Color> data_;
};

} // namespace raster

#include <vector>
#include <cstdint>
#include <iostream>
#include <fstream>

struct Pixel {
    uint8_t r, g, b, a;
};

void save_ppm(const char* filename,
    const std::vector<Pixel>& pixels,
    int width, int height) {
    std::ofstream file(filename);

    // 写入PPM头部
    file << "P3\n";              // P3 = ASCII格式的PPM
    file << width << " " << height << "\n";
    file << "255\n";             // 最大颜色值

    // 写入像素数据
    for (const auto& p : pixels) {
        file << (int)p.r << " "
            << (int)p.g << " "
            << (int)p.b << " ";
    }
}

int main() {

    std::vector<Pixel> images(100 * 100, { 255, 0, 0, 255 });

    images[50 * 100 + 50] = Pixel{ 0, 255,0,255 };
    save_ppm("image.ppm", images, 100, 100);

    // gradient
    for (int i = 0; i < 100; i++) {
        for (int j = 0; j < 100; j++) {
            images[i * 100 + j] = Pixel{ static_cast<uint8_t>(i), static_cast<uint8_t>(j), 0, 255 };
        }
    }
    save_ppm("gradient.ppm", images, 100, 100);

    return 0;
}

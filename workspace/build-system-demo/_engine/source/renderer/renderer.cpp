// Engine Renderer 实现
#include <iostream>

namespace Engine {

void InitRenderer() {
    std::cout << "  [Engine::Renderer] Initialized" << std::endl;
    std::cout << "    - DirectX 12 Backend: OK" << std::endl;
    std::cout << "    - Shader Compiler: OK" << std::endl;
    std::cout << "    - Render Graph: OK" << std::endl;
}

} // namespace Engine

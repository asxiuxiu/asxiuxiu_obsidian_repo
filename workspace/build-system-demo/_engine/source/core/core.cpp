// Engine Core 实现
#include <iostream>

namespace Engine {

void InitCore() {
    std::cout << "  [Engine::Core] Initialized" << std::endl;
    std::cout << "    - Memory Manager: OK" << std::endl;
    std::cout << "    - File System: OK" << std::endl;
    std::cout << "    - Logger: OK" << std::endl;
}

} // namespace Engine

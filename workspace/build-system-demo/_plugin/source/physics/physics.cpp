// Physics Plugin 实现
#include <iostream>

namespace Plugin {

void InitPhysics() {
    std::cout << "  [Plugin::Physics] Initialized" << std::endl;
    std::cout << "    - PhysX Integration: OK" << std::endl;
    std::cout << "    - Collision System: OK" << std::endl;
    std::cout << "    - Rigid Body Manager: OK" << std::endl;
}

} // namespace Plugin

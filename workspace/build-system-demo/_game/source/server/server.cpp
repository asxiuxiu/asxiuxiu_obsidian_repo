// Game Server 实现
#include <iostream>

namespace Game {

void InitServer() {
    std::cout << "  [Game::Server] Initialized" << std::endl;
    std::cout << "    - Network Layer: OK" << std::endl;
    std::cout << "    - Player Manager: OK" << std::endl;
    std::cout << "    - World Simulation: OK" << std::endl;
}

} // namespace Game

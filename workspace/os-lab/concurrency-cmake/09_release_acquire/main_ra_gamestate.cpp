// 实验09b：用版本号发布批量数据
// 对应笔记：Release-Acquire——最实用的跨线程同步

#include <atomic>
#include <thread>
#include <iostream>
#include <chrono>

struct GameState {
    float player_x = 0.0f;
    float player_y = 0.0f;
    int health = 100;
    int ammo = 30;
};

GameState g_state;
std::atomic<int> g_version{0};

void update_thread() {
    for (int frame = 1; frame <= 5; ++frame) {
        g_state.player_x += 1.0f;
        g_state.player_y += 2.0f;
        g_state.health -= 5;
        g_state.ammo -= 1;
        g_version.fetch_add(1, std::memory_order_release);
        std::cout << "[Update] Frame " << frame
                  << " version=" << g_version.load() << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void render_thread() {
    int local_version = 0;
    int frames_rendered = 0;
    while (frames_rendered < 5) {
        int ver = g_version.load(std::memory_order_acquire);
        if (ver != local_version) {
            local_version = ver;
            std::cout << "[Render] Version " << ver
                      << ": pos=(" << g_state.player_x
                      << ", " << g_state.player_y << ")"
                      << " health=" << g_state.health
                      << " ammo=" << g_state.ammo << "\n";
            ++frames_rendered;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

int main() {
    std::thread t1(update_thread);
    std::thread t2(render_thread);
    t1.join(); t2.join();
    return 0;
}

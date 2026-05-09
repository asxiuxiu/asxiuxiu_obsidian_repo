// flags: -O0 -g
#include <iostream>
#include <cstring>

struct PlayerData {
    char  id;
    int   score;
    char  flag;
};

int main() {
    unsigned char raw[6] = { 0x01, 0x00, 0x00, 0x00, 0x64, 0x00 };
    PlayerData p;
    std::memcpy(&p, raw, 6);
    std::cout << "sizeof(PlayerData) = " << sizeof(PlayerData) << "\n";
    std::cout << "expected (1+4+1)   = " << (1 + 4 + 1) << "\n";
    std::cout << "score = " << p.score << "\n";
    return 0;
}
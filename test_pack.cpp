// flags: -O0 -g
#include <iostream>

#pragma pack(push, 1)
struct PackedPlayer {
    char  id;
    int   score;
    char  flag;
};
#pragma pack(pop)

struct NormalPlayer {
    char  id;
    int   score;
    char  flag;
};

int main() {
    std::cout << "sizeof(NormalPlayer) = " << sizeof(NormalPlayer) << "\n";
    std::cout << "sizeof(PackedPlayer) = " << sizeof(PackedPlayer) << "\n";
    return 0;
}
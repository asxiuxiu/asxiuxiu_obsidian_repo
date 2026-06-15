int main() {
    int x = 1;
    int y = 2;
    // x = 1 sequenced-before y = 2 吗？不一定，没有数据依赖时可能重排
    return 0;
}
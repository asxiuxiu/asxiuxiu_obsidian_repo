int main() {
    int a = 1;
    int b = a + 2;  // a = 1 sequenced-before b = 3
    return 0;
}
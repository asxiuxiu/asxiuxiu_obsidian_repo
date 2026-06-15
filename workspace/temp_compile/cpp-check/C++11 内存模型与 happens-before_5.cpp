int main() {
    // 同线程
    int a = 1;      // A
    int b = a + 1;  // B, A sequenced-before B, 所以 A happens-before B

    // 跨线程（通过锁）—— 概念示意
    // thread1: data = 42; lock.release();   // A, release
    // thread2: lock.acquire(); read data;   // B, acquire
    // A synchronizes-with B, 所以 A happens-before B
    return 0;
}
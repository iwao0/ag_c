// unsigned int の減算ラップ (10-20 → 0xFFFFFFF6 = 4294967286)
// 期待: exit=1
int main(void) {
    unsigned int x = 10;
    unsigned int y = 20;
    unsigned int z = x - y;
    return z == 4294967286u;
}

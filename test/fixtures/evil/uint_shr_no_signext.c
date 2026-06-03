// unsigned int の右シフトは sign extension しない
// 0x80000000 >> 1 == 0x40000000
// 期待: exit=1
int main(void) {
    unsigned int x = 0x80000000u;
    unsigned int y = x >> 1;
    return y == 0x40000000u;
}

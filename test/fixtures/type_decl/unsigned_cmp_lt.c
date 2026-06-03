// unsigned int の比較で MSB ある値 > 1
// 期待: exit=1
int main(void) {
    unsigned int a = (1u << 31) | 1;
    return (a > 1u) ? 1 : 0;
}

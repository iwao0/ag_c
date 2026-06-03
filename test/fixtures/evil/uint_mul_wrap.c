// unsigned int の乗算ラップ (65536*65536 == 0)
// 期待: exit=1
int main(void) {
    unsigned int x = 65536u;
    unsigned int y = x * x;
    return y == 0;
}

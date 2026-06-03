// short に 65535 → -1 (符号拡張)
// 期待: exit=1
int main(void) {
    short s = 65535;
    return (s < 0) ? 1 : 0;
}

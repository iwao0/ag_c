// signed char に 255 → -1 (符号拡張で負)
// 期待: exit=1
int main(void) {
    char c = 255;
    return (c < 0) ? 1 : 0;
}

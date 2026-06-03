// signed char のオーバーフロー (127+1 → -128)
// 期待: exit=1
int main(void) {
    char c = 127;
    c = c + 1;
    return c < 0;
}

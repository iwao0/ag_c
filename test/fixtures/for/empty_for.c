// `for(;;)` 無限ループを break で抜ける
// 期待: exit=5
int main(void) {
    int i = 0;
    for (;;) {
        if (i >= 5) break;
        i = i + 1;
    }
    return i;
}

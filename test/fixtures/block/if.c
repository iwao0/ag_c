// if-else 本体にブロック (複数文 + return)
// 期待: exit=5
main() {
    if (1) {
        a = 2;
        b = 3;
        return a + b;
    } else {
        return 0;
    }
}

// for ループの本体にブロック
// 期待: exit=55
main() {
    a = 0;
    b = 0;
    for (a = 1; a <= 10; a = a + 1) { b = b + a; }
    return b;
}

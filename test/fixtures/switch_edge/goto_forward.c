// 前方 goto
// 期待: exit=42
main() {
    goto L1;
    return 0;
L1:
    return 42;
}

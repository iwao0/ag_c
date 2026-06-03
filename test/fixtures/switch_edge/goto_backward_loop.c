// 後方 goto でループを作る
// 期待: exit=3
main() {
    i = 0;
L:
    i = i + 1;
    if (i < 3) goto L;
    return i;
}

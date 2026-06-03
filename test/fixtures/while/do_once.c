// do-while: 本体が必ず 1 回は実行される
// 期待: exit=1
main() {
    a = 0;
    do a = a + 1;
    while (0);
    return a;
}

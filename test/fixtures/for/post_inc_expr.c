// for ループ更新部に後置インクリメントを使う
// 期待: exit=5
main() {
    a = 0;
    for (a = 0; a < 5; a++) a;
    return a;
}

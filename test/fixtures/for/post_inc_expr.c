// for ループ更新部に後置インクリメントを使う
// 期待: exit=5
int main(void) {
    int a;
    for (a = 0; a < 5; a++) a;
    return a;
}

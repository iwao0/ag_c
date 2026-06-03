// タグ定義と同時にポインタ変数宣言
// 期待: exit=1
int main(void) {
    struct S { int x; } *p;
    p = 0;
    return p == 0;
}

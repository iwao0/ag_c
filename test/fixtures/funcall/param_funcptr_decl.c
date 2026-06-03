// 関数ポインタ仮引数の宣言 (受理確認)
// 期待: exit=7
int apply(int (*fp)(int), int x) { return x; }
int main(void) {
    return apply(0, 7);
}

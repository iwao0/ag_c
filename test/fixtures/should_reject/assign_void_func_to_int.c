// void 関数の戻り値を int 変数に代入は不正。
// 期待: ag_c は型エラー
void f(void) {}
int main(void) { int x; x = f(); return 0; }

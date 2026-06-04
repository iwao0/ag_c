// void 戻り値関数の戻り値を変数初期化に使うのは不正。
// 期待: ag_c は void value not ignored エラー
void f(void) {}
int main(void) { int x = f(); return x; }

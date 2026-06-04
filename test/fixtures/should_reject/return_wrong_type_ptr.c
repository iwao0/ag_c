// ポインタを返す関数が整数を返している (型不整合)。
// 期待: ag_c は戻り値型エラー
int *f(void) { return 5; }
int main(void) { return 0; }

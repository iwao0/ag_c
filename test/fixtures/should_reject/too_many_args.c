// 関数の引数が多すぎる (C11 6.5.2.2p6: 引数数は仮引数数と一致するべき)。
// 期待: ag_c は引数数エラー
int add(int a, int b) { return a + b; }
int main(void) { return add(1, 2, 3); }

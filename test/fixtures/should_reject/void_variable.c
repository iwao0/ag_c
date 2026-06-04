// void 型の変数宣言は不正 (`void` はインスタンス化できない型)。
// 期待: ag_c は型エラー
int main(void) { void x; return 0; }

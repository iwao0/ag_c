// 整数値を deref は不正 (`*` の引数はポインタ型でなければならない)。
// 期待: ag_c は型エラー
int main(void) { int x = 5; return *x; }

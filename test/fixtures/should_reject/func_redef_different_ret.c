// 同名関数の再宣言で戻り値型が異なるのは不正 (C11 6.7p3)。
// 期待: ag_c は宣言衝突エラー
int f(void);
char f(void) { return 1; }
int main(void) { return 0; }

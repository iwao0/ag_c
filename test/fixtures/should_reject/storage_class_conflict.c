// `static extern` のような相反する storage class 指定は不正 (C11 6.7.1p2)。
// 期待: ag_c は宣言エラー
int main(void) { static extern int x; return 0; }

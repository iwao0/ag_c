// 関数ポインタ typedef を仮引数に使う
// dbl(7) = 14
// 期待: exit=14
typedef int (*fp_t)(int);
int dbl(int x) { return x * 2; }
int apply(fp_t f, int x) { return f(x); }
int main(void) { return apply(dbl, 7); }

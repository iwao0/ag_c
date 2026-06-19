// 関数ポインタ typedef を仮引数に使う
// dbl(7) = 14
// 期待: exit=14
#include <assert.h>
typedef int (*fp_t)(int);
int dbl(int x) { return x * 2; }
int apply(fp_t f, int x) { return f(x); }
int main(void) { assert(apply(dbl, 7) == 14); return 0; }

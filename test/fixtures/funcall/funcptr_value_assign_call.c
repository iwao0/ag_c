// 関数ポインタ変数への代入と呼び出し
// 期待: exit=42 (41+1)
#include <assert.h>
int inc(int x) { return x + 1; }
int main(void) {
    int (*fp)(int);
    fp = inc;
    assert(fp(41) == 42);
    return 0;
}

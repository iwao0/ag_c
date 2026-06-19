// 整数リテラルの LL / U サフィックス
// 期待: exit=6 (1+5)
#include <assert.h>
int main(void) {
    long long v = 1LL;
    unsigned int u = 5U;
    assert((int)(v + u) == 6);
    return 0;
}

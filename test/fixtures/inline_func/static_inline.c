// static inline 関数の呼び出し
#include <assert.h>
static inline int mul(int a, int b) { return a * b; }
int main(void) {
    assert(mul(6, 7) == 42);
    return 0;
}

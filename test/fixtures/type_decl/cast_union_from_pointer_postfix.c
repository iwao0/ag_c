// ポインタから union への cast + メンバ比較
// 期待: exit=1
#include <assert.h>
int main(void) {
    union U { int *p; int q; };
    int x = 3;
    assert(((union U)&x).p == &x);
    return 0;
}

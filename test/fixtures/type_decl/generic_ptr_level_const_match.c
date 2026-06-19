// int * const * マッチ (2 段ポインタの const 区別)
// 期待: exit=0
#include <assert.h>
int main(void) {
    int x = 0;
    int *p = &x;
    int * const *pp = &p;
    assert(_Generic(pp, int**: 1, int * const *: 2, default: 3) == 2);
    return 0;
}

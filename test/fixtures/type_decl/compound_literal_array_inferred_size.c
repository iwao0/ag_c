// 複合リテラル (T[]){...} の要素数推定
// 期待: exit=0
#include <assert.h>
int main(void) {
    int *p = (int[]){1, 2, 3, 4};
    assert(p[0] == 1);
    assert(p[1] == 2);
    assert(p[2] == 3);
    assert(p[3] == 4);
    return 0;
}

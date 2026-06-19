// int * volatile * マッチ
// 期待: exit=0
#include <assert.h>
int main(void) {
    int x = 0;
    int *p = &x;
    int * volatile *pp = &p;
    assert(_Generic(pp, int**: 1, int * volatile *: 2, default: 3) == 2);
    return 0;
}

// const int* と int* の区別
// 期待: exit=0
#include <assert.h>
int main(void) {
    int x = 0;
    const int *p = &x;
    assert(_Generic(p, int*: 1, const int*: 2, default: 3) == 2);
    return 0;
}

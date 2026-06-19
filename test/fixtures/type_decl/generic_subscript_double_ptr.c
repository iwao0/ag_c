// double*[0] で double にマッチ
// 期待: exit=0
#include <assert.h>
int main(void) {
    double a[1] = {1.0};
    double *p = a;
    assert(_Generic(p[0], double: 42, default: 99) == 42);
    return 0;
}

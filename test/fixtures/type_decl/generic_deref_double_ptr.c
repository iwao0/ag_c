// double* の deref で double にマッチ
// 期待: exit=0
#include <assert.h>
int main(void) {
    double d = 1.0;
    double *p = &d;
    assert(_Generic(*p, double: 42, default: 99) == 42);
    return 0;
}

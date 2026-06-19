// _Generic で double* マッチ
// 期待: exit=0
#include <assert.h>
int main(void) {
    double d = 1.0;
    double *pd = &d;
    assert(_Generic(pd, int*: 1, double*: 2, default: 3) == 2);
    return 0;
}

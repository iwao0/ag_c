// float* の deref で float にマッチ
// 期待: exit=0
#include <assert.h>
int main(void) {
    float f = 1.0f;
    float *p = &f;
    assert(_Generic(*p, float: 11, default: 99) == 11);
    return 0;
}

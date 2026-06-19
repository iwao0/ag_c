// float.h の DBL_EPSILON は (0, 1) の正値
// 期待: exit=42
#include <float.h>
#include <assert.h>
int main(void) {
    double e = DBL_EPSILON;
    assert(e > 0.0);
    assert(e < 1.0);
    return 0;
}

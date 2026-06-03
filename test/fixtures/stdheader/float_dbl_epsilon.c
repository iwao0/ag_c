// float.h の DBL_EPSILON は (0, 1) の正値
// 期待: exit=42
#include <float.h>
int main(void) {
    double e = DBL_EPSILON;
    return e > 0.0 && e < 1.0 ? 42 : 0;
}

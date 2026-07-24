// float.h long double macro completeness, types, and value boundaries.
// Expected: exit=0
#include <float.h>

int main(void) {
    volatile int max_kind = _Generic(
        LDBL_MAX, long double: 1, double: 2, default: 0);
    volatile int min_kind = _Generic(
        LDBL_MIN, long double: 1, double: 2, default: 0);
    volatile int epsilon_kind = _Generic(
        LDBL_EPSILON, long double: 1, double: 2, default: 0);
    volatile int true_min_kind = _Generic(
        LDBL_TRUE_MIN, long double: 1, double: 2, default: 0);
    volatile long double one = 1.0L;
    volatile long double epsilon = LDBL_EPSILON;
    volatile long double minimum = LDBL_MIN;
    volatile long double true_minimum = LDBL_TRUE_MIN;
    volatile long double maximum = LDBL_MAX;

    if (sizeof(long double) != 8 || _Alignof(long double) != 8)
        return 1;
    if (LDBL_MANT_DIG != 53 || LDBL_DIG != 15 ||
        LDBL_MIN_EXP != -1021 || LDBL_MAX_EXP != 1024 ||
        LDBL_MIN_10_EXP != -307 || LDBL_MAX_10_EXP != 308 ||
        LDBL_DECIMAL_DIG != 17 || DECIMAL_DIG != LDBL_DECIMAL_DIG ||
        LDBL_HAS_SUBNORM != 1)
        return 2;
    if (max_kind != 1 || min_kind != 1 ||
        epsilon_kind != 1 || true_min_kind != 1)
        return 3;
    if (!(maximum > one) || !(minimum > 0.0L) ||
        !(true_minimum > 0.0L) || !(true_minimum < minimum) ||
        one + epsilon == one)
        return 4;
    if (LDBL_MAX != (long double)DBL_MAX ||
        LDBL_MIN != (long double)DBL_MIN ||
        LDBL_EPSILON != (long double)DBL_EPSILON ||
        LDBL_TRUE_MIN != (long double)DBL_TRUE_MIN)
        return 5;
    return 0;
}

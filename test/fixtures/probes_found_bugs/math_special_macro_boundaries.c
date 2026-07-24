// math.h special floating constants and math_errhandling boundaries.
// Expected: exit=0
#include <math.h>

#ifndef HUGE_VAL
#error "math.h must define HUGE_VAL"
#endif
#ifndef HUGE_VALF
#error "math.h must define HUGE_VALF"
#endif
#ifndef HUGE_VALL
#error "math.h must define HUGE_VALL"
#endif
#ifndef INFINITY
#error "math.h must define INFINITY"
#endif
#ifndef NAN
#error "math.h must define NAN"
#endif
#ifndef math_errhandling
#error "math.h must define math_errhandling"
#endif

#if MATH_ERRNO != 1 || MATH_ERREXCEPT != 2
#error "math error mechanism bits must be integer constant expressions"
#endif

static double global_huge = HUGE_VAL;
static float global_huge_float = HUGE_VALF;
static long double global_huge_long_double = HUGE_VALL;
static float global_infinity = INFINITY;
static float global_nan = NAN;

int main(void) {
    int handling = math_errhandling;
    volatile float nan_copy = global_nan;

    if (!_Generic(HUGE_VAL, double: 1, default: 0) ||
        !_Generic(HUGE_VALF, float: 1, default: 0) ||
        !_Generic(HUGE_VALL, long double: 1, default: 0) ||
        !_Generic(INFINITY, float: 1, default: 0) ||
        !_Generic(NAN, float: 1, default: 0))
        return 1;
    if (fpclassify(global_huge) != FP_INFINITE ||
        fpclassify(global_huge_float) != FP_INFINITE ||
        fpclassify(global_huge_long_double) != FP_INFINITE ||
        fpclassify(global_infinity) != FP_INFINITE)
        return 2;
    if (signbit(global_huge) || signbit(global_huge_float) ||
        signbit(global_huge_long_double) || signbit(global_infinity))
        return 3;
    if (!isnan(global_nan) || nan_copy == global_nan)
        return 4;
    if ((handling & ~(MATH_ERRNO | MATH_ERREXCEPT)) != 0 ||
        (handling & (MATH_ERRNO | MATH_ERREXCEPT)) == 0)
        return 5;
#ifdef __wasm32__
    if (handling != MATH_ERREXCEPT)
        return 6;
#endif
    return 0;
}

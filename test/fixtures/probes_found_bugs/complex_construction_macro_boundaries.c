// complex.h C11 construction macro type, value, and single-evaluation boundaries.
// Expected: exit=0
#include <complex.h>

#ifndef CMPLX
#error "complex.h must define CMPLX"
#endif
#ifndef CMPLXF
#error "complex.h must define CMPLXF"
#endif
#ifndef CMPLXL
#error "complex.h must define CMPLXL"
#endif

#define IS_DOUBLE_COMPLEX(value) \
    _Generic((value), double _Complex: 1, default: 0)
#define IS_FLOAT_COMPLEX(value) \
    _Generic((value), float _Complex: 1, default: 0)
#define IS_LONG_DOUBLE_COMPLEX(value) \
    _Generic((value), long double _Complex: 1, default: 0)

static double next_double(int *count, double value) {
    ++*count;
    return value;
}

static float next_float(int *count, float value) {
    ++*count;
    return value;
}

static long double next_long_double(int *count, long double value) {
    ++*count;
    return value;
}

int main(void) {
    int double_real_count = 0;
    int double_imag_count = 0;
    int float_real_count = 0;
    int float_imag_count = 0;
    int long_real_count = 0;
    int long_imag_count = 0;
    double _Complex double_value =
        CMPLX(next_double(&double_real_count, 1.25),
              next_double(&double_imag_count, -2.5));
    float _Complex float_value =
        CMPLXF(next_float(&float_real_count, 3.5f),
               next_float(&float_imag_count, -4.25f));
    long double _Complex long_double_value =
        CMPLXL(next_long_double(&long_real_count, 5.75L),
               next_long_double(&long_imag_count, -6.5L));

    if (!IS_DOUBLE_COMPLEX(CMPLX(0, 0)) ||
        !IS_FLOAT_COMPLEX(CMPLXF(0, 0)) ||
        !IS_LONG_DOUBLE_COMPLEX(CMPLXL(0, 0)))
        return 1;
    if (double_real_count != 1 || double_imag_count != 1 ||
        float_real_count != 1 || float_imag_count != 1 ||
        long_real_count != 1 || long_imag_count != 1)
        return 2;
    if (__real__ double_value != 1.25 || __imag__ double_value != -2.5)
        return 3;
    if (__real__ float_value != 3.5f || __imag__ float_value != -4.25f)
        return 4;
    if (__real__ long_double_value != 5.75L ||
        __imag__ long_double_value != -6.5L)
        return 5;
    return 0;
}

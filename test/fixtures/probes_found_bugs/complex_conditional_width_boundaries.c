// Complex conditional-expression width conversion and branch evaluation.
// Expected: exit=0
#include <complex.h>

#define IS_FLOAT_COMPLEX(value) \
    _Generic((value), float complex: 1, default: 0)
#define IS_DOUBLE_COMPLEX(value) \
    _Generic((value), double complex: 1, default: 0)
#define IS_LONG_DOUBLE_COMPLEX(value) \
    _Generic((value), long double complex: 1, default: 0)

static int next_condition(int *count, int value) {
    ++*count;
    return value;
}

static float complex make_float(
    int *count, float real, float imaginary_part) {
    ++*count;
    return CMPLXF(real, imaginary_part);
}

static double complex make_double(
    int *count, double real, double imaginary_part) {
    ++*count;
    return CMPLX(real, imaginary_part);
}

static long double complex make_long_double(
    int *count, long double real, long double imaginary_part) {
    ++*count;
    return CMPLXL(real, imaginary_part);
}

static long double complex choose_return(
    int condition, int *true_count, int *false_count) {
    return condition
        ? make_float(true_count, 7.5f, -8.25f)
        : make_long_double(false_count, -9.5L, 10.25L);
}

int main(void) {
    int condition_count = 0;
    int true_count = 0;
    int false_count = 0;
    int complex_true_count = 0;
    int complex_false_count = 0;
    double complex double_result;
    long double complex long_double_result;

    if (!IS_DOUBLE_COMPLEX(
            1 ? CMPLXF(1.0f, 2.0f) : CMPLX(3.0, 4.0)) ||
        !IS_LONG_DOUBLE_COMPLEX(
            1 ? CMPLX(1.0, 2.0) : CMPLXL(3.0L, 4.0L)) ||
        !IS_FLOAT_COMPLEX(
            1 ? 3 : CMPLXF(1.0f, 2.0f)) ||
        !IS_LONG_DOUBLE_COMPLEX(
            1 ? CMPLXF(1.0f, 2.0f) : 3.0L)) {
        return 1;
    }

    double_result =
        next_condition(&condition_count, 1)
            ? make_float(&true_count, 1.25f, -2.5f)
            : make_double(&false_count, 99.0, 100.0);
    if (condition_count != 1 || true_count != 1 || false_count != 0 ||
        creal(double_result) != 1.25 || cimag(double_result) != -2.5) {
        return 2;
    }

    long_double_result =
        next_condition(&condition_count, 0)
            ? make_double(&true_count, 101.0, 102.0)
            : make_long_double(&false_count, 3.75L, -4.5L);
    if (condition_count != 2 || true_count != 1 || false_count != 1 ||
        creall(long_double_result) != 3.75L ||
        cimagl(long_double_result) != -4.5L) {
        return 3;
    }

    long_double_result = choose_return(1, &true_count, &false_count);
    if (true_count != 2 || false_count != 1 ||
        creall(long_double_result) != 7.5L ||
        cimagl(long_double_result) != -8.25L) {
        return 4;
    }
    long_double_result = choose_return(0, &true_count, &false_count);
    if (true_count != 2 || false_count != 2 ||
        creall(long_double_result) != -9.5L ||
        cimagl(long_double_result) != 10.25L) {
        return 5;
    }

    double_result =
        CMPLXF(0.0f, 1.0f)
            ? make_float(&complex_true_count, 11.5f, -12.25f)
            : make_double(&complex_false_count, 103.0, 104.0);
    if (complex_true_count != 1 || complex_false_count != 0 ||
        creal(double_result) != 11.5 ||
        cimag(double_result) != -12.25) {
        return 6;
    }
    long_double_result =
        CMPLX(0.0, 0.0)
            ? make_double(&complex_true_count, 105.0, 106.0)
            : make_long_double(
                &complex_false_count, -13.5L, 14.25L);
    if (complex_true_count != 1 || complex_false_count != 1 ||
        creall(long_double_result) != -13.5L ||
        cimagl(long_double_result) != 14.25L) {
        return 7;
    }
    return 0;
}

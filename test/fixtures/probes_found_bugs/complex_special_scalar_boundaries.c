// Complex scalar conversions and comparisons with IEEE special values.
// Expected: exit=0
#include <complex.h>
#include <math.h>

static double complex static_nan_real = CMPLX(NAN, 0.0);
static double complex static_nan_imaginary = CMPLX(0.0, NAN);
static double complex static_infinite_real = CMPLX(INFINITY, -0.0);
static double complex static_infinite_imaginary = CMPLX(-0.0, -INFINITY);
static double complex static_signed_zero = CMPLX(-0.0, 0.0);

static _Bool static_nan_real_truth = CMPLX(NAN, 0.0);
static _Bool static_nan_imaginary_truth = CMPLX(0.0, NAN);
static _Bool static_signed_zero_truth = CMPLX(-0.0, 0.0);
static double static_nan_real_scalar = CMPLX(NAN, 12.0);
static double static_signed_zero_scalar = CMPLX(-0.0, 13.0);
static int static_nan_equal =
    CMPLX(NAN, 0.0) == CMPLX(NAN, 0.0);
static int static_nan_unequal =
    CMPLX(0.0, NAN) != CMPLX(0.0, NAN);
static int static_zero_equal =
    CMPLX(-0.0, 0.0) == CMPLX(0.0, -0.0);

static double complex make_value(double real, double imag) {
    return CMPLX(real, imag);
}

int main(void) {
    volatile double zero = 0.0;
    double runtime_nan = zero / zero;
    double runtime_infinity = 1.0 / zero;
    double complex nan_real = make_value(runtime_nan, 0.0);
    double complex nan_imaginary = make_value(0.0, runtime_nan);
    double complex infinite_real = make_value(runtime_infinity, -0.0);
    double complex infinite_imaginary =
        make_value(-0.0, -runtime_infinity);
    double complex signed_zero = make_value(-0.0, 0.0);

    if (!isnan(creal(static_nan_real)) ||
        !isnan(cimag(static_nan_imaginary)) ||
        !isinf(creal(static_infinite_real)) ||
        !isinf(cimag(static_infinite_imaginary)) ||
        !signbit(cimag(static_infinite_imaginary)) ||
        !signbit(creal(static_signed_zero)))
        return 1;
    if (static_nan_real_truth != 1 ||
        static_nan_imaginary_truth != 1 ||
        static_signed_zero_truth != 0 ||
        !isnan(static_nan_real_scalar) ||
        !signbit(static_signed_zero_scalar))
        return 2;
    if (static_nan_equal != 0 ||
        static_nan_unequal != 1 ||
        static_zero_equal != 1)
        return 3;
    if ((_Bool)nan_real != 1 ||
        (_Bool)nan_imaginary != 1 ||
        (_Bool)signed_zero != 0 ||
        (_Bool)infinite_real != 1 ||
        (_Bool)infinite_imaginary != 1)
        return 4;
    if (!isnan((double)nan_real) ||
        !signbit((double)signed_zero) ||
        !isinf((double)infinite_real))
        return 5;
    if (nan_real == nan_real ||
        nan_imaginary == nan_imaginary ||
        !(nan_real != nan_real) ||
        !(nan_imaginary != nan_imaginary) ||
        signed_zero != 0.0)
        return 6;
    return 0;
}

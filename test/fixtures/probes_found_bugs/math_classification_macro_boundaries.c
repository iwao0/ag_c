#include <math.h>

union float_bits {
    float value;
    unsigned int bits;
};

static int float_call_count;
static int double_call_count;
static int long_double_call_count;

static float next_float(float value) {
    float_call_count++;
    return value;
}

static double next_double(double value) {
    double_call_count++;
    return value;
}

static long double next_long_double(long double value) {
    long_double_call_count++;
    return value;
}

int main(void) {
    union float_bits float_subnormal = {.bits = 1U};
    union float_bits float_infinity = {.bits = 0x7f800000U};
    union float_bits float_nan = {.bits = 0x7fc00000U};
    union float_bits float_negative_zero = {.bits = 0x80000000U};
    double zero = 0.0;
    double double_subnormal = 5e-324;
    double double_infinity = 1.0 / zero;
    double double_nan = zero / zero;
    double double_negative_zero = -zero;

    if (fpclassify(float_nan.value) != FP_NAN ||
        fpclassify(float_infinity.value) != FP_INFINITE ||
        fpclassify(0.0f) != FP_ZERO ||
        fpclassify(float_subnormal.value) != FP_SUBNORMAL ||
        fpclassify(1.0f) != FP_NORMAL) {
        return 1;
    }

    if (fpclassify(double_nan) != FP_NAN) return 20;
    if (fpclassify(double_infinity) != FP_INFINITE) return 21;
    if (fpclassify(0.0) != FP_ZERO) return 22;
    if (fpclassify(double_subnormal) != FP_SUBNORMAL) return 23;
    if (fpclassify(1.0) != FP_NORMAL) return 24;

    if (fpclassify(0.0L) != FP_ZERO ||
        fpclassify(1.0L) != FP_NORMAL ||
        !isfinite(1.0L) || !isnormal(1.0L)) {
        return 3;
    }

    if (!isnan(float_nan.value) || !isnan(double_nan) ||
        !isinf(float_infinity.value) || !isinf(double_infinity) ||
        isfinite(float_infinity.value) || isfinite(double_nan) ||
        isnormal(float_subnormal.value) || isnormal(double_subnormal)) {
        return 4;
    }

    if (!signbit(float_negative_zero.value) ||
        !signbit(double_negative_zero) ||
        signbit(0.0f) || signbit(0.0)) {
        return 5;
    }

    if (!isgreater(3.0f, 2.0L) ||
        !isgreaterequal(3.0L, 3.0f) ||
        !isless(2.0f, 3.0L) ||
        !islessequal(3.0L, 3.0f) ||
        !islessgreater(2.0f, 3.0L) ||
        islessgreater(3.0L, 3.0f)) {
        return 6;
    }

    if (!isunordered(float_nan.value, 1.0L) ||
        !isunordered(1.0f, double_nan) ||
        isunordered(1.0f, 2.0L) ||
        isgreater(float_nan.value, 1.0) ||
        isless(1.0, double_nan)) {
        return 7;
    }

    float_call_count = 0;
    if (!isfinite(next_float(1.0f)) || float_call_count != 1) return 8;

    double_call_count = 0;
    if (fpclassify(next_double(double_subnormal)) != FP_SUBNORMAL ||
        double_call_count != 1) {
        return 9;
    }

    float_call_count = 0;
    long_double_call_count = 0;
    if (!isgreater(next_float(3.0f), next_long_double(2.0L)) ||
        float_call_count != 1 || long_double_call_count != 1) {
        return 10;
    }

    float_call_count = 0;
    double_call_count = 0;
    if (!isunordered(next_float(float_nan.value), next_double(1.0)) ||
        float_call_count != 1 || double_call_count != 1) {
        return 11;
    }

    return 0;
}

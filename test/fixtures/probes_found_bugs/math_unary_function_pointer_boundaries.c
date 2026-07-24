#include <math.h>
#include <stddef.h>

typedef double (*double_unary_fn)(double);
typedef float (*float_unary_fn)(float);
typedef long double (*long_double_unary_fn)(long double);

struct double_case {
    double_unary_fn function;
    double input;
    double expected;
};

struct float_case {
    float_unary_fn function;
    float input;
    float expected;
};

struct long_double_case {
    long_double_unary_fn function;
    long double input;
    long double expected;
};

static struct double_case double_cases[] = {
    {acos, 1.0, 0.0},
    {asin, 0.0, 0.0},
    {atan, 0.0, 0.0},
    {cos, 0.0, 1.0},
    {sin, 0.0, 0.0},
    {tan, 0.0, 0.0},
    {cosh, 0.0, 1.0},
    {sinh, 0.0, 0.0},
    {tanh, 0.0, 0.0},
    {acosh, 1.0, 0.0},
    {asinh, 0.0, 0.0},
    {atanh, 0.0, 0.0},
    {exp, 0.0, 1.0},
    {exp2, 0.0, 1.0},
    {expm1, 0.0, 0.0},
    {erf, 0.0, 0.0},
    {erfc, 0.0, 1.0},
    {log, 1.0, 0.0},
    {log1p, 0.0, 0.0},
    {log10, 1.0, 0.0},
    {log2, 1.0, 0.0},
    {sqrt, 4.0, 2.0},
    {cbrt, 8.0, 2.0},
    {ceil, 1.25, 2.0},
    {floor, 1.75, 1.0},
    {round, -1.5, -2.0},
    {trunc, -1.75, -1.0},
    {nearbyint, 1.25, 1.0},
    {rint, 1.25, 1.0},
    {fabs, -2.5, 2.5},
};

static struct float_case float_cases[] = {
    {acosf, 1.0f, 0.0f},
    {asinf, 0.0f, 0.0f},
    {atanf, 0.0f, 0.0f},
    {cosf, 0.0f, 1.0f},
    {sinf, 0.0f, 0.0f},
    {tanf, 0.0f, 0.0f},
    {coshf, 0.0f, 1.0f},
    {sinhf, 0.0f, 0.0f},
    {tanhf, 0.0f, 0.0f},
    {acoshf, 1.0f, 0.0f},
    {asinhf, 0.0f, 0.0f},
    {atanhf, 0.0f, 0.0f},
    {expf, 0.0f, 1.0f},
    {exp2f, 0.0f, 1.0f},
    {expm1f, 0.0f, 0.0f},
    {erff, 0.0f, 0.0f},
    {erfcf, 0.0f, 1.0f},
    {logf, 1.0f, 0.0f},
    {log1pf, 0.0f, 0.0f},
    {log10f, 1.0f, 0.0f},
    {log2f, 1.0f, 0.0f},
    {sqrtf, 4.0f, 2.0f},
    {cbrtf, 8.0f, 2.0f},
    {ceilf, 1.25f, 2.0f},
    {floorf, 1.75f, 1.0f},
    {roundf, -1.5f, -2.0f},
    {truncf, -1.75f, -1.0f},
    {nearbyintf, 1.25f, 1.0f},
    {rintf, 1.25f, 1.0f},
    {fabsf, -2.5f, 2.5f},
};

static struct long_double_case long_double_cases[] = {
    {acosl, 1.0L, 0.0L},
    {asinl, 0.0L, 0.0L},
    {atanl, 0.0L, 0.0L},
    {cosl, 0.0L, 1.0L},
    {sinl, 0.0L, 0.0L},
    {tanl, 0.0L, 0.0L},
    {coshl, 0.0L, 1.0L},
    {sinhl, 0.0L, 0.0L},
    {tanhl, 0.0L, 0.0L},
    {acoshl, 1.0L, 0.0L},
    {asinhl, 0.0L, 0.0L},
    {atanhl, 0.0L, 0.0L},
    {expl, 0.0L, 1.0L},
    {exp2l, 0.0L, 1.0L},
    {expm1l, 0.0L, 0.0L},
    {erfl, 0.0L, 0.0L},
    {erfcl, 0.0L, 1.0L},
    {logl, 1.0L, 0.0L},
    {log1pl, 0.0L, 0.0L},
    {log10l, 1.0L, 0.0L},
    {log2l, 1.0L, 0.0L},
    {sqrtl, 4.0L, 2.0L},
    {cbrtl, 8.0L, 2.0L},
    {ceill, 1.25L, 2.0L},
    {floorl, 1.75L, 1.0L},
    {roundl, -1.5L, -2.0L},
    {truncl, -1.75L, -1.0L},
    {nearbyintl, 1.25L, 1.0L},
    {rintl, 1.25L, 1.0L},
    {fabsl, -2.5L, 2.5L},
};

static double absolute_double(double value) {
    return value < 0.0 ? -value : value;
}

static long double absolute_long_double(long double value) {
    return value < 0.0L ? -value : value;
}

int main(void) {
    size_t double_count = sizeof(double_cases) / sizeof(double_cases[0]);
    size_t float_count = sizeof(float_cases) / sizeof(float_cases[0]);
    size_t long_double_count =
        sizeof(long_double_cases) / sizeof(long_double_cases[0]);

    if (double_count != 30 || float_count != 30 ||
        long_double_count != 30) {
        return 1;
    }

    for (size_t i = 0; i < double_count; i++) {
        if (!double_cases[i].function) return 2;
        double actual = double_cases[i].function(double_cases[i].input);
        if (absolute_double(actual - double_cases[i].expected) > 0.000001) {
            return 3 + (int)i;
        }
    }

    for (size_t i = 0; i < float_count; i++) {
        if (!float_cases[i].function) return 33;
        float actual = float_cases[i].function(float_cases[i].input);
        if (absolute_double(
                (double)actual - (double)float_cases[i].expected) > 0.0001) {
            return 34 + (int)i;
        }
    }

    for (size_t i = 0; i < long_double_count; i++) {
        if (!long_double_cases[i].function) return 64;
        long double actual =
            long_double_cases[i].function(long_double_cases[i].input);
        if (absolute_long_double(
                actual - long_double_cases[i].expected) > 0.000001L) {
            return 65 + (int)i;
        }
    }

    return 0;
}

// complex.h float/long-double elementary-function pointer and ABI boundaries.
// Expected: exit=0
#include <complex.h>

typedef float complex (*float_unary_fn)(float complex);
typedef float complex (*float_binary_fn)(float complex, float complex);
typedef long double complex (*long_double_unary_fn)(long double complex);
typedef long double complex (*long_double_binary_fn)(
    long double complex, long double complex);

static float_unary_fn float_unary_functions[] = {
    csinhf, ccoshf, ctanf, ctanhf
};
static float_binary_fn float_binary_function = cpowf;
static long_double_unary_fn long_double_unary_functions[] = {
    cexpl, clogl, csqrtl, csinl, ccosl, csinhl, ccoshl, ctanl, ctanhl
};
static long_double_binary_fn long_double_binary_function = cpowl;

static long double absolute(long double value) {
    return value < 0.0L ? -value : value;
}

static int near_complex(
    long double complex actual,
    long double expected_real,
    long double expected_imaginary,
    long double tolerance) {
    return absolute(creall(actual) - expected_real) < tolerance &&
           absolute(cimagl(actual) - expected_imaginary) < tolerance;
}

int main(void) {
    static const long double expected_float[4][2] = {
        {0.4573041531842493L, 0.5406126857131534L},
        {0.9895848833999199L, 0.2498263975004615L},
        {0.4038964553160258L, 0.5640831412674985L},
        {0.5640831412674985L, 0.4038964553160258L},
    };
    static const long double expected_long_double[9][2] = {
        {1.4468890365841691L, 0.7904390832136149L},
        {-0.3465735902799727L, 0.7853981633974483L},
        {2.0L, 1.0L},
        {0.5406126857131534L, 0.4573041531842493L},
        {0.9895848833999199L, -0.2498263975004615L},
        {0.4573041531842493L, 0.5406126857131534L},
        {0.9895848833999199L, 0.2498263975004615L},
        {0.4038964553160258L, 0.5640831412674985L},
        {0.5640831412674985L, 0.4038964553160258L},
    };
    float complex float_value = CMPLXF(0.5f, 0.5f);
    long double complex long_double_value = CMPLXL(0.5L, 0.5L);

    for (int i = 0; i < 4; ++i) {
        if (!float_unary_functions[i] ||
            !near_complex(
                float_unary_functions[i](float_value),
                expected_float[i][0], expected_float[i][1], 0.0002L)) {
            return 1 + i;
        }
    }
    if (!float_binary_function ||
        !near_complex(
            float_binary_function(CMPLXF(1.0f, 2.0f), CMPLXF(2.0f, 0.0f)),
            -3.0L, 4.0L, 0.0002L)) {
        return 5;
    }

    for (int i = 0; i < 9; ++i) {
        long double complex input =
            i == 2 ? CMPLXL(3.0L, 4.0L) : long_double_value;
        if (!long_double_unary_functions[i] ||
            !near_complex(
                long_double_unary_functions[i](input),
                expected_long_double[i][0],
                expected_long_double[i][1], 0.000001L)) {
            return 6 + i;
        }
    }
    if (!long_double_binary_function ||
        !near_complex(
            long_double_binary_function(
                CMPLXL(1.0L, 2.0L), CMPLXL(2.0L, 0.0L)),
            -3.0L, 4.0L, 0.000001L)) {
        return 15;
    }
    return 0;
}

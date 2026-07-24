#include <math.h>
#include <stddef.h>

typedef double (*double_binary_fn)(double, double);
typedef float (*float_binary_fn)(float, float);
typedef long double (*long_double_binary_fn)(long double, long double);

struct double_binary_case {
    double_binary_fn function;
    double left;
    double right;
    double expected;
};

struct float_binary_case {
    float_binary_fn function;
    float left;
    float right;
    float expected;
};

struct long_double_binary_case {
    long_double_binary_fn function;
    long double left;
    long double right;
    long double expected;
};

static struct double_binary_case double_binary_cases[] = {
    {atan2, 0.0, 1.0, 0.0},
    {pow, 2.0, 3.0, 8.0},
    {fmod, -5.5, 2.0, -1.5},
    {remainder, 5.5, 2.0, -0.5},
    {fdim, 2.0, 5.0, 0.0},
    {copysign, 3.0, -1.0, -3.0},
    {hypot, 3.0, 4.0, 5.0},
    {fmin, 2.0, 3.0, 2.0},
    {fmax, 2.0, 3.0, 3.0},
};

static struct float_binary_case float_binary_cases[] = {
    {atan2f, 0.0f, 1.0f, 0.0f},
    {powf, 2.0f, 3.0f, 8.0f},
    {fmodf, -5.5f, 2.0f, -1.5f},
    {remainderf, 5.5f, 2.0f, -0.5f},
    {fdimf, 2.0f, 5.0f, 0.0f},
    {copysignf, 3.0f, -1.0f, -3.0f},
    {hypotf, 3.0f, 4.0f, 5.0f},
    {fminf, 2.0f, 3.0f, 2.0f},
    {fmaxf, 2.0f, 3.0f, 3.0f},
};

static struct long_double_binary_case long_double_binary_cases[] = {
    {atan2l, 0.0L, 1.0L, 0.0L},
    {powl, 2.0L, 3.0L, 8.0L},
    {fmodl, -5.5L, 2.0L, -1.5L},
    {remainderl, 5.5L, 2.0L, -0.5L},
    {fdiml, 2.0L, 5.0L, 0.0L},
    {copysignl, 3.0L, -1.0L, -3.0L},
    {hypotl, 3.0L, 4.0L, 5.0L},
    {fminl, 2.0L, 3.0L, 2.0L},
    {fmaxl, 2.0L, 3.0L, 3.0L},
};

static double (*fma_signature)(double, double, double) = fma;
static float (*fmaf_signature)(float, float, float) = fmaf;
static long double (*fmal_signature)(
    long double, long double, long double) = fmal;

static double (*ldexp_signature)(double, int) = ldexp;
static float (*ldexpf_signature)(float, int) = ldexpf;
static long double (*ldexpl_signature)(long double, int) = ldexpl;
static double (*scalbn_signature)(double, int) = scalbn;
static float (*scalbnf_signature)(float, int) = scalbnf;
static long double (*scalbnl_signature)(long double, int) = scalbnl;
static double (*scalbln_signature)(double, long) = scalbln;
static float (*scalblnf_signature)(float, long) = scalblnf;
static long double (*scalblnl_signature)(long double, long) = scalblnl;

static int (*ilogb_signature)(double) = ilogb;
static int (*ilogbf_signature)(float) = ilogbf;
static int (*ilogbl_signature)(long double) = ilogbl;
static double (*logb_signature)(double) = logb;
static float (*logbf_signature)(float) = logbf;
static long double (*logbl_signature)(long double) = logbl;

static long (*lrint_signature)(double) = lrint;
static long (*lrintf_signature)(float) = lrintf;
static long (*lrintl_signature)(long double) = lrintl;
static long long (*llrint_signature)(double) = llrint;
static long long (*llrintf_signature)(float) = llrintf;
static long long (*llrintl_signature)(long double) = llrintl;
static long (*lround_signature)(double) = lround;
static long (*lroundf_signature)(float) = lroundf;
static long (*lroundl_signature)(long double) = lroundl;
static long long (*llround_signature)(double) = llround;
static long long (*llroundf_signature)(float) = llroundf;
static long long (*llroundl_signature)(long double) = llroundl;

static double (*nan_signature)(const char *) = nan;
static float (*nanf_signature)(const char *) = nanf;
static long double (*nanl_signature)(const char *) = nanl;

static double absolute_double(double value) {
    return value < 0.0 ? -value : value;
}

static long double absolute_long_double(long double value) {
    return value < 0.0L ? -value : value;
}

int main(void) {
    size_t double_count =
        sizeof(double_binary_cases) / sizeof(double_binary_cases[0]);
    size_t float_count =
        sizeof(float_binary_cases) / sizeof(float_binary_cases[0]);
    size_t long_double_count =
        sizeof(long_double_binary_cases) / sizeof(long_double_binary_cases[0]);

    if (double_count != 9 || float_count != 9 || long_double_count != 9) {
        return 1;
    }
    for (size_t i = 0; i < double_count; i++) {
        if (!double_binary_cases[i].function) return 2;
        double actual = double_binary_cases[i].function(
            double_binary_cases[i].left, double_binary_cases[i].right);
        if (absolute_double(actual - double_binary_cases[i].expected) >
            0.000001) {
            return 3 + (int)i;
        }
    }
    for (size_t i = 0; i < float_count; i++) {
        if (!float_binary_cases[i].function) return 12;
        float actual = float_binary_cases[i].function(
            float_binary_cases[i].left, float_binary_cases[i].right);
        if (absolute_double(
                (double)actual - (double)float_binary_cases[i].expected) >
            0.0001) {
            return 13 + (int)i;
        }
    }
    for (size_t i = 0; i < long_double_count; i++) {
        if (!long_double_binary_cases[i].function) return 22;
        long double actual = long_double_binary_cases[i].function(
            long_double_binary_cases[i].left,
            long_double_binary_cases[i].right);
        if (absolute_long_double(
                actual - long_double_binary_cases[i].expected) > 0.000001L) {
            return 23 + (int)i;
        }
    }

    if (!fma_signature || !fmaf_signature || !fmal_signature) return 32;
    if (fma_signature(2.0, 3.0, 4.0) != 10.0 ||
        fmaf_signature(2.0f, 3.0f, 4.0f) != 10.0f ||
        fmal_signature(2.0L, 3.0L, 4.0L) != 10.0L) {
        return 33;
    }

    if (!ldexp_signature || !ldexpf_signature || !ldexpl_signature ||
        !scalbn_signature || !scalbnf_signature || !scalbnl_signature ||
        !scalbln_signature || !scalblnf_signature || !scalblnl_signature) {
        return 34;
    }
    if (absolute_double(ldexp_signature(1.5, 2) - 6.0) > 0.000001 ||
        absolute_double((double)ldexpf_signature(1.5f, 2) - 6.0) >
            0.0001 ||
        absolute_long_double(ldexpl_signature(1.5L, 2) - 6.0L) >
            0.000001L ||
        absolute_double(scalbn_signature(1.5, -1) - 0.75) > 0.000001 ||
        absolute_double((double)scalbnf_signature(1.5f, -1) - 0.75) >
            0.0001 ||
        absolute_long_double(scalbnl_signature(1.5L, -1) - 0.75L) >
            0.000001L ||
        absolute_double(scalbln_signature(1.5, 3L) - 12.0) > 0.000001 ||
        absolute_double((double)scalblnf_signature(1.5f, 3L) - 12.0) >
            0.0001 ||
        absolute_long_double(scalblnl_signature(1.5L, 3L) - 12.0L) >
            0.000001L) {
        return 35;
    }

    if (!ilogb_signature || !ilogbf_signature || !ilogbl_signature ||
        !logb_signature || !logbf_signature || !logbl_signature) {
        return 36;
    }
    if (ilogb_signature(8.0) != 3 ||
        ilogbf_signature(8.0f) != 3 ||
        ilogbl_signature(8.0L) != 3 ||
        logb_signature(8.0) != 3.0 ||
        logbf_signature(8.0f) != 3.0f ||
        logbl_signature(8.0L) != 3.0L) {
        return 37;
    }

    if (!lrint_signature || !lrintf_signature || !lrintl_signature ||
        !llrint_signature || !llrintf_signature || !llrintl_signature ||
        !lround_signature || !lroundf_signature || !lroundl_signature ||
        !llround_signature || !llroundf_signature || !llroundl_signature) {
        return 38;
    }
    if (lrint_signature(1.25) != 1L ||
        lrintf_signature(1.25f) != 1L ||
        lrintl_signature(1.25L) != 1L ||
        llrint_signature(2.25) != 2LL ||
        llrintf_signature(2.25f) != 2LL ||
        llrintl_signature(2.25L) != 2LL ||
        lround_signature(-1.5) != -2L ||
        lroundf_signature(-1.5f) != -2L ||
        lroundl_signature(-1.5L) != -2L ||
        llround_signature(2.5) != 3LL ||
        llroundf_signature(2.5f) != 3LL ||
        llroundl_signature(2.5L) != 3LL) {
        return 39;
    }

    if (!nan_signature || !nanf_signature || !nanl_signature) return 40;
    {
        double double_nan = nan_signature("");
        float float_nan = nanf_signature("");
        long double long_double_nan = nanl_signature("");
        if (double_nan > 0.0 || double_nan <= 0.0 ||
            float_nan > 0.0f || float_nan <= 0.0f ||
            long_double_nan > 0.0L || long_double_nan <= 0.0L) {
            return 41;
        }
    }

    return 0;
}

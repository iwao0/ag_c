// math.h output pointer API function pointer and quotient sign boundaries
// Expected: exit=0
#include <math.h>

int main(void) {
    double (*split_double)(double, int *) = frexp;
    float (*split_float)(float, int *) = frexpf;
    long double (*split_long_double)(long double, int *) = frexpl;
    double (*fraction_double)(double, double *) = modf;
    float (*fraction_float)(float, float *) = modff;
    long double (*fraction_long_double)(long double, long double *) = modfl;
    double (*remainder_quotient_double)(double, double, int *) = remquo;
    float (*remainder_quotient_float)(float, float, int *) = remquof;
    long double (*remainder_quotient_long_double)(long double, long double,
                                                  int *) = remquol;
    int exponent = 0;
    int quotient;
    double double_whole = 0.0;
    float float_whole = 0.0f;
    long double long_double_whole = 0.0L;
    double double_result;
    float float_result;
    long double long_double_result;

    double_result = split_double(-6.0, &exponent);
    if (double_result != -0.75 || exponent != 3) return 1;
    float_result = split_float(3.0f, &exponent);
    if (float_result != 0.75f || exponent != 2) return 2;
    long_double_result = split_long_double(0.25L, &exponent);
    if (long_double_result != 0.5L || exponent != -1) return 3;

    double_result = fraction_double(-2.25, &double_whole);
    if (double_result != -0.25 || double_whole != -2.0) return 4;
    float_result = fraction_float(-2.25f, &float_whole);
    if (float_result != -0.25f || float_whole != -2.0f) return 5;
    long_double_result = fraction_long_double(-2.25L, &long_double_whole);
    if (long_double_result != -0.25L || long_double_whole != -2.0L) return 6;

    quotient = 0;
    double_result = remainder_quotient_double(-5.5, 2.0, &quotient);
    if (double_result != 0.5 || quotient >= 0 || ((-quotient) & 7) != 3) return 7;

    quotient = 0;
    float_result = remainder_quotient_float(-5.5f, 2.0f, &quotient);
    if (float_result != 0.5f || quotient >= 0 || ((-quotient) & 7) != 3) return 8;

    quotient = 0;
    long_double_result =
        remainder_quotient_long_double(-5.5L, 2.0L, &quotient);
    if (long_double_result != 0.5L || quotient >= 0 ||
        ((-quotient) & 7) != 3) {
        return 9;
    }
    return 0;
}

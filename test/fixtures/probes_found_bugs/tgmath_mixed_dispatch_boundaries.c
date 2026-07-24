#include <tgmath.h>

#define TYPE_CODE(value) \
    _Generic((value), float: 1, double: 2, long double: 3, default: 0)

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

static double absolute_double(double value) {
    return value < 0.0 ? -value : value;
}

static int same_type_code(int actual, int expected) {
    return actual == expected;
}

int main(void) {
    int quotient_bits = 0;

    if (!same_type_code(TYPE_CODE(sqrt(4.0f)), 1) ||
        !same_type_code(TYPE_CODE(sqrt(4.0)), 2) ||
        !same_type_code(TYPE_CODE(sqrt(4.0L)), 3) ||
        !same_type_code(TYPE_CODE(sqrt(4)), 2)) {
        return 1;
    }

    if (!same_type_code(TYPE_CODE(pow(2.0f, 3.0f)), 1) ||
        !same_type_code(TYPE_CODE(pow(2.0f, 3.0)), 2) ||
        !same_type_code(TYPE_CODE(pow(2.0, 3.0L)), 3) ||
        !same_type_code(TYPE_CODE(pow(2, 3.0f)), 2)) {
        return 2;
    }

    if (!same_type_code(TYPE_CODE(fma(2.0f, 3.0f, 4.0L)), 3) ||
        !same_type_code(TYPE_CODE(fma(2.0f, 3.0, 4.0f)), 2) ||
        !same_type_code(TYPE_CODE(fma(2, 3.0f, 4.0f)), 2)) {
        return 3;
    }

    if (!same_type_code(
            TYPE_CODE(remquo(5.5f, 2.0, &quotient_bits)), 2) ||
        !same_type_code(
            TYPE_CODE(remquo(5.5, 2.0L, &quotient_bits)), 3)) {
        return 4;
    }

    /*
     * The integer exponent and output-pointer parameters are not generic
     * parameters and must not widen the result selected from the first input.
     */
    if (!same_type_code(TYPE_CODE(ldexp(1.5f, 2)), 1) ||
        !same_type_code(TYPE_CODE(scalbln(1.5f, 3L)), 1)) {
        return 5;
    }

    float_call_count = 0;
    double_call_count = 0;
    if (absolute_double(
            pow(next_float(2.0f), next_double(3.0)) - 8.0) > 0.000001 ||
        float_call_count != 1 || double_call_count != 1) {
        return 6;
    }

    float_call_count = 0;
    double_call_count = 0;
    long_double_call_count = 0;
    if (fma(next_float(2.0f), next_double(3.0),
            next_long_double(4.0L)) != 10.0L ||
        float_call_count != 1 || double_call_count != 1 ||
        long_double_call_count != 1) {
        return 7;
    }

    float_call_count = 0;
    double_call_count = 0;
    quotient_bits = 0;
    if (absolute_double(
            remquo(next_float(5.5f), next_double(2.0), &quotient_bits) +
            0.5) > 0.000001 ||
        quotient_bits != 3 || float_call_count != 1 ||
        double_call_count != 1) {
        return 8;
    }

    return 0;
}

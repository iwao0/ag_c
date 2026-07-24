// File-scope complex brace, compound-literal, and arithmetic initializers.
// Expected: exit=0
#include <complex.h>

static float complex brace_float = {1.0f, -2.0f};
static double complex brace_double = {3.0, -4.0};
static long double complex brace_long_double = {5.0L, -6.0L};

static float complex unit = I;
static float complex compound_float = CMPLXF(7.0f, -8.0f);
static double complex compound_double = CMPLX(9.0, -10.0);
static long double complex compound_long_double = CMPLXL(11.0L, -12.0L);

static float complex arithmetic_float = 13.0f + 14.0f * I;
static double complex arithmetic_double = 15.0 + 16.0 * I;
static long double complex arithmetic_long_double = 17.0L + 18.0L * I;

static double complex product =
    CMPLX(2.0, 3.0) * CMPLX(4.0, -5.0);
static double complex quotient =
    (CMPLX(23.0, 2.0) / CMPLX(1.0, -1.0));
static double complex negated = -CMPLX(19.0, -20.0);
static double complex selected =
    CMPLX(0.0, 1.0) ? CMPLX(21.0, -22.0) : CMPLX(0.0, 0.0);

static float complex complex_array[] = {
    CMPLXF(23.0f, -24.0f),
    25.0f + 26.0f * I,
};

struct complex_holder {
    int prefix;
    double complex value;
    int suffix;
};

static struct complex_holder holder = {
    27,
    CMPLX(28.0, -29.0),
    30,
};

struct complex_array_holder {
    int prefix;
    float complex values[2];
    int suffix;
};

static struct complex_array_holder array_holder = {
    31,
    {CMPLXF(32.0f, -33.0f), CMPLXF(34.0f, -35.0f)},
    36,
};

union complex_union {
    double complex value;
    unsigned char bytes[16];
};

static union complex_union union_value = {
    CMPLX(37.0, -38.0),
};

union complex_array_union {
    float complex values[2];
    unsigned char bytes[16];
};

static union complex_array_union union_array = {
    {CMPLXF(39.0f, -40.0f), CMPLXF(41.0f, -42.0f)},
};

static int check_static_locals(void) {
    static float complex local_unit = I;
    static double complex local_value =
        CMPLX(43.0, -44.0) + CMPLX(0.0, 2.0);
    static struct complex_holder local_holder = {
        45,
        CMPLX(46.0, -47.0),
        48,
    };
    return crealf(local_unit) == 0.0f &&
           cimagf(local_unit) == 1.0f &&
           creal(local_value) == 43.0 &&
           cimag(local_value) == -42.0 &&
           local_holder.prefix == 45 &&
           creal(local_holder.value) == 46.0 &&
           cimag(local_holder.value) == -47.0 &&
           local_holder.suffix == 48;
}

int main(void) {
    if (crealf(brace_float) != 1.0f || cimagf(brace_float) != -2.0f)
        return 1;
    if (creal(brace_double) != 3.0 || cimag(brace_double) != -4.0)
        return 2;
    if (creall(brace_long_double) != 5.0L ||
        cimagl(brace_long_double) != -6.0L)
        return 3;
    if (crealf(unit) != 0.0f || cimagf(unit) != 1.0f)
        return 4;
    if (crealf(compound_float) != 7.0f ||
        cimagf(compound_float) != -8.0f)
        return 5;
    if (creal(compound_double) != 9.0 ||
        cimag(compound_double) != -10.0)
        return 6;
    if (creall(compound_long_double) != 11.0L ||
        cimagl(compound_long_double) != -12.0L)
        return 7;
    if (crealf(arithmetic_float) != 13.0f ||
        cimagf(arithmetic_float) != 14.0f)
        return 8;
    if (creal(arithmetic_double) != 15.0 ||
        cimag(arithmetic_double) != 16.0)
        return 9;
    if (creall(arithmetic_long_double) != 17.0L ||
        cimagl(arithmetic_long_double) != 18.0L)
        return 10;
    if (creal(product) != 23.0 || cimag(product) != 2.0)
        return 11;
    if (creal(quotient) != 10.5 || cimag(quotient) != 12.5)
        return 12;
    if (creal(negated) != -19.0 || cimag(negated) != 20.0)
        return 13;
    if (creal(selected) != 21.0 || cimag(selected) != -22.0)
        return 14;
    if (crealf(complex_array[0]) != 23.0f ||
        cimagf(complex_array[0]) != -24.0f ||
        crealf(complex_array[1]) != 25.0f ||
        cimagf(complex_array[1]) != 26.0f)
        return 15;
    if (holder.prefix != 27 ||
        creal(holder.value) != 28.0 ||
        cimag(holder.value) != -29.0 ||
        holder.suffix != 30)
        return 16;
    if (array_holder.prefix != 31 ||
        crealf(array_holder.values[0]) != 32.0f ||
        cimagf(array_holder.values[0]) != -33.0f ||
        crealf(array_holder.values[1]) != 34.0f ||
        cimagf(array_holder.values[1]) != -35.0f ||
        array_holder.suffix != 36)
        return 17;
    if (creal(union_value.value) != 37.0 ||
        cimag(union_value.value) != -38.0)
        return 18;
    if (crealf(union_array.values[0]) != 39.0f ||
        cimagf(union_array.values[0]) != -40.0f ||
        crealf(union_array.values[1]) != 41.0f ||
        cimagf(union_array.values[1]) != -42.0f)
        return 19;
    if (!check_static_locals())
        return 20;
    return 0;
}

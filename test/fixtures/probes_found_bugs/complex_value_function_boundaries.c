// complex.h value-function address, single-evaluation, and projection boundaries.
// Expected: exit=0
#include <complex.h>
#include <math.h>

typedef double (*double_real_fn)(double complex);
typedef float (*float_real_fn)(float complex);
typedef long double (*long_double_real_fn)(long double complex);
typedef double complex (*double_complex_fn)(double complex);
typedef float complex (*float_complex_fn)(float complex);
typedef long double complex (*long_double_complex_fn)(long double complex);

static double_real_fn double_real_functions[] = {cabs, carg};
static float_real_fn float_real_functions[] = {cabsf, cargf};
static long_double_real_fn long_double_real_functions[] = {cabsl, cargl};
static double_complex_fn double_complex_functions[] = {conj, cproj};
static float_complex_fn float_complex_functions[] = {conjf, cprojf};
static long_double_complex_fn long_double_complex_functions[] = {
    conjl, cprojl
};

static double complex next_double_complex(int *count) {
    ++*count;
    return CMPLX(3.0, 4.0);
}

static float complex next_float_complex(int *count) {
    ++*count;
    return CMPLXF(3.0f, 4.0f);
}

static long double complex next_long_double_complex(int *count) {
    ++*count;
    return CMPLXL(3.0L, 4.0L);
}

int main(void) {
    int double_count = 0;
    int float_count = 0;
    int long_double_count = 0;
    double complex projected;
    float complex projected_float;
    long double complex projected_long_double;

    if (cabs(next_double_complex(&double_count)) != 5.0 ||
        carg(next_double_complex(&double_count)) <= 0.9 ||
        creal(conj(next_double_complex(&double_count))) != 3.0 ||
        creal(cproj(next_double_complex(&double_count))) != 3.0 ||
        double_count != 4) {
        return 1;
    }
    if (cabsf(next_float_complex(&float_count)) != 5.0f ||
        cargf(next_float_complex(&float_count)) <= 0.9f ||
        crealf(conjf(next_float_complex(&float_count))) != 3.0f ||
        crealf(cprojf(next_float_complex(&float_count))) != 3.0f ||
        float_count != 4) {
        return 2;
    }
    if (cabsl(next_long_double_complex(&long_double_count)) != 5.0L ||
        cargl(next_long_double_complex(&long_double_count)) <= 0.9L ||
        creall(conjl(next_long_double_complex(&long_double_count))) != 3.0L ||
        creall(cprojl(next_long_double_complex(&long_double_count))) != 3.0L ||
        long_double_count != 4) {
        return 3;
    }

    if (double_real_functions[0](CMPLX(3.0, 4.0)) != 5.0 ||
        double_real_functions[1](CMPLX(0.0, 2.0)) <= 1.5 ||
        cimag(double_complex_functions[0](CMPLX(3.0, 4.0))) != -4.0) {
        return 4;
    }
    if (float_real_functions[0](CMPLXF(3.0f, 4.0f)) != 5.0f ||
        float_real_functions[1](CMPLXF(0.0f, 2.0f)) <= 1.5f ||
        cimagf(float_complex_functions[0](CMPLXF(3.0f, 4.0f))) != -4.0f) {
        return 5;
    }
    if (long_double_real_functions[0](CMPLXL(3.0L, 4.0L)) != 5.0L ||
        long_double_real_functions[1](CMPLXL(0.0L, 2.0L)) <= 1.5L ||
        cimagl(long_double_complex_functions[0](
            CMPLXL(3.0L, 4.0L))) != -4.0L) {
        return 6;
    }

    projected = double_complex_functions[1](CMPLX(2.0, -HUGE_VAL));
    projected_float =
        float_complex_functions[1](CMPLXF(-HUGE_VALF, 2.0f));
    projected_long_double =
        long_double_complex_functions[1](CMPLXL(2.0L, -HUGE_VALL));
    if (!isinf(creal(projected)) || signbit(creal(projected)) ||
        cimag(projected) != 0.0 || !signbit(cimag(projected))) {
        return 7;
    }
    if (!isinf(crealf(projected_float)) || signbit(crealf(projected_float)) ||
        cimagf(projected_float) != 0.0f || signbit(cimagf(projected_float))) {
        return 8;
    }
    if (!isinf(creall(projected_long_double)) ||
        signbit(creall(projected_long_double)) ||
        cimagl(projected_long_double) != 0.0L ||
        !signbit(cimagl(projected_long_double))) {
        return 9;
    }
    return 0;
}

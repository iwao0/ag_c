// complex.h component-function address and imaginary-unit type boundaries.
// Expected: exit=0
#include <complex.h>
#include <math.h>

typedef double (*double_component_fn)(double complex);
typedef float (*float_component_fn)(float complex);
typedef long double (*long_double_component_fn)(long double complex);

static double_component_fn double_components[] = {creal, cimag};
static float_component_fn float_components[] = {crealf, cimagf};
static long_double_component_fn long_double_components[] = {creall, cimagl};

static double complex next_double_complex(int *count) {
    ++*count;
    return CMPLX(-2.5, 3.75);
}

static float complex next_float_complex(int *count) {
    ++*count;
    return CMPLXF(-4.5f, 5.25f);
}

static long double complex next_long_double_complex(int *count) {
    ++*count;
    return CMPLXL(-6.5L, 7.25L);
}

int main(void) {
    int double_count = 0;
    int float_count = 0;
    int long_double_count = 0;
    int complex_i_type =
        _Generic(_Complex_I, float complex: 1, default: 0);
    int i_type = _Generic(I, float complex: 1, default: 0);
    int float_sum_type =
        _Generic(1.0f + I, float complex: 1, default: 0);
    int double_sum_type =
        _Generic(1.0 + I, double complex: 1, default: 0);
    int long_double_sum_type =
        _Generic(1.0L + I, long double complex: 1, default: 0);
    double negative_real_zero;
    double negative_imaginary_zero;

    if (!complex_i_type || !i_type || !float_sum_type ||
        !double_sum_type || !long_double_sum_type ||
        crealf(_Complex_I) != 0.0f || cimagf(_Complex_I) != 1.0f) {
        return 1;
    }
    if (creal(next_double_complex(&double_count)) != -2.5 ||
        cimag(next_double_complex(&double_count)) != 3.75 ||
        double_count != 2) {
        return 2;
    }
    if (crealf(next_float_complex(&float_count)) != -4.5f ||
        cimagf(next_float_complex(&float_count)) != 5.25f ||
        float_count != 2) {
        return 3;
    }
    if (creall(next_long_double_complex(&long_double_count)) != -6.5L ||
        cimagl(next_long_double_complex(&long_double_count)) != 7.25L ||
        long_double_count != 2) {
        return 4;
    }

    if (double_components[0](CMPLX(8.5, -9.25)) != 8.5 ||
        double_components[1](CMPLX(8.5, -9.25)) != -9.25 ||
        float_components[0](CMPLXF(10.5f, -11.25f)) != 10.5f ||
        float_components[1](CMPLXF(10.5f, -11.25f)) != -11.25f ||
        long_double_components[0](
            CMPLXL(12.5L, -13.25L)) != 12.5L ||
        long_double_components[1](
            CMPLXL(12.5L, -13.25L)) != -13.25L) {
        return 5;
    }

    negative_real_zero = double_components[0](CMPLX(-0.0, 1.0));
    negative_imaginary_zero = double_components[1](CMPLX(1.0, -0.0));
    if (!signbit(negative_real_zero) || !signbit(negative_imaginary_zero))
        return 6;
    return 0;
}

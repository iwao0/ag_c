#include <complex.h>

typedef double complex (*double_complex_unary_fn)(double complex);
typedef double complex (*double_complex_binary_fn)(
    double complex, double complex);
typedef float complex (*float_complex_unary_fn)(float complex);

static double_complex_unary_fn double_unary_functions[] = {
    cexp, clog, csqrt, csin, ccos, csinh, ccosh, ctan, ctanh
};
static double_complex_binary_fn double_binary_function = cpow;
static float_complex_unary_fn float_unary_functions[] = {
    cexpf, clogf, csqrtf, csinf, ccosf
};

static double absolute(double value) {
    return value < 0.0 ? -value : value;
}

static int near_double(double actual, double expected) {
    return absolute(actual - expected) < 0.000001;
}

static int near_float(float actual, float expected) {
    return absolute((double)actual - (double)expected) < 0.0002;
}

static int check_double_complex(
    double complex actual, double expected_real, double expected_imaginary) {
    return near_double(creal(actual), expected_real) &&
           near_double(cimag(actual), expected_imaginary);
}

static int check_float_complex(
    float complex actual, float expected_real, float expected_imaginary) {
    return near_float(crealf(actual), expected_real) &&
           near_float(cimagf(actual), expected_imaginary);
}

int main(void) {
    for (int i = 0; i < 9; i++) {
        if (!double_unary_functions[i]) return 1;
    }
    if (!double_binary_function) return 2;
    for (int i = 0; i < 5; i++) {
        if (!float_unary_functions[i]) return 3;
    }

    {
        double complex z = 1.0 + 2.0 * I;
        double complex result = double_unary_functions[0](z);
        if (!check_double_complex(
                result, -1.1312043837568135, 2.4717266720048188)) {
            return 4;
        }
    }
    {
        double complex z = 3.0 + 4.0 * I;
        double complex result = double_unary_functions[1](z);
        if (!check_double_complex(
                result, 1.6094379124341003, 0.9272952180016122)) {
            return 5;
        }
        result = double_unary_functions[2](z);
        if (!check_double_complex(result, 2.0, 1.0)) return 6;
    }
    {
        double complex z = 0.5 + 0.5 * I;
        static const double expected[6][2] = {
            {0.5406126857131534, 0.4573041531842493},
            {0.9895848833999199, -0.2498263975004615},
            {0.4573041531842493, 0.5406126857131534},
            {0.9895848833999199, 0.2498263975004615},
            {0.4038964553160258, 0.5640831412674985},
            {0.5640831412674985, 0.4038964553160258},
        };
        for (int i = 0; i < 6; i++) {
            double complex result = double_unary_functions[i + 3](z);
            if (!check_double_complex(
                    result, expected[i][0], expected[i][1])) {
                return 7 + i;
            }
        }
    }
    {
        double complex base = 1.0 + 2.0 * I;
        double complex exponent = 2.0 + 0.0 * I;
        double complex result = double_binary_function(base, exponent);
        if (!check_double_complex(result, -3.0, 4.0)) return 13;
    }

    {
        float complex z = 0.5f + 0.5f * I;
        float complex result = float_unary_functions[0](z);
        if (!check_float_complex(result, 1.4468890f, 0.7904391f)) return 14;
        result = float_unary_functions[1](z);
        if (!check_float_complex(result, -0.3465736f, 0.7853982f)) return 15;
        result = float_unary_functions[2](3.0f + 4.0f * I);
        if (!check_float_complex(result, 2.0f, 1.0f)) return 16;
        result = float_unary_functions[3](z);
        if (!check_float_complex(result, 0.5406127f, 0.4573042f)) return 17;
        result = float_unary_functions[4](z);
        if (!check_float_complex(result, 0.9895849f, -0.2498264f)) return 18;
    }

    return 0;
}

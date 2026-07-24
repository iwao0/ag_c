// Runtime complex-to-scalar conversions and mixed equality boundaries.
// Expected: exit=0
#include <complex.h>

struct complex_box {
    double complex value;
};

static double complex make_value(double real, double imag) {
    return CMPLX(real, imag);
}

static double take_double(double value) {
    return value;
}

static int take_int(int value) {
    return value;
}

static _Bool take_bool(_Bool value) {
    return value;
}

int main(void) {
    double complex value = make_value(3.75, -4.5);
    struct complex_box box = {make_value(5.25, -6.5)};
    double complex values[2] = {
        make_value(7.5, -8.25),
        make_value(9.75, -10.5),
    };

    double implicit_real = value;
    float explicit_float = (float)value;
    int explicit_int = (int)value;
    _Bool implicit_truth = value;
    _Bool imaginary_truth = make_value(0.0, 11.0);
    _Bool zero_truth = make_value(0.0, 0.0);

    if (implicit_real != 3.75 ||
        explicit_float != 3.75f ||
        explicit_int != 3 ||
        implicit_truth != 1 ||
        imaginary_truth != 1 ||
        zero_truth != 0)
        return 1;
    if (take_double(box.value) != 5.25 ||
        take_int((int)values[0]) != 7 ||
        take_bool(values[1]) != 1)
        return 2;
    if (value != make_value(3.75, -4.5) ||
        value == make_value(3.75, 4.5) ||
        value != 3.75 - 4.5 * I ||
        value == 3.75)
        return 3;
    return 0;
}

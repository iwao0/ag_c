// Runtime complex-to-scalar conversions across language contexts.
// Expected: exit=0
#include <complex.h>
#include <stdatomic.h>

struct signed_bits {
    signed int value : 6;
};

static double complex make_value(double real, double imag) {
    return CMPLX(real, imag);
}

static double return_double(void) {
    return make_value(1.25, -2.5);
}

static int return_int(void) {
    return make_value(3.75, -4.5);
}

static _Bool return_bool(int imaginary_only) {
    return imaginary_only ? make_value(0.0, 5.5)
                          : make_value(0.0, 0.0);
}

int main(void) {
    double assigned = 0.0;
    double assignment_result =
        (assigned = make_value(6.25, -7.5));
    int integer = 0;
    int integer_result =
        (integer = make_value(8.75, -9.5));
    _Bool truth = 0;
    _Bool truth_result =
        (truth = make_value(0.0, 10.5));
    struct signed_bits bits = {0};
    int bitfield_result =
        (bits.value = make_value(11.75, -12.5));
    volatile double volatile_value = 0.0;
    double volatile_result =
        (volatile_value = make_value(13.25, -14.5));
    _Atomic double atomic_value = 0.0;
    double atomic_result =
        (atomic_value = make_value(15.25, -16.5));
    int comma_count = 0;
    double comma_value =
        (comma_count++, make_value(17.25, -18.5));
    double conditional_true =
        1 ? make_value(19.25, -20.5) : 21.25;
    double conditional_false =
        0 ? make_value(22.25, -23.5) : 24.25;

    if (return_double() != 1.25 ||
        return_int() != 3 ||
        return_bool(1) != 1 ||
        return_bool(0) != 0)
        return 1;
    if (assigned != 6.25 || assignment_result != 6.25 ||
        integer != 8 || integer_result != 8 ||
        truth != 1 || truth_result != 1)
        return 2;
    if (bits.value != 11 || bitfield_result != 11 ||
        volatile_value != 13.25 || volatile_result != 13.25 ||
        atomic_value != 15.25 || atomic_result != 15.25)
        return 3;
    if (comma_count != 1 || comma_value != 17.25 ||
        conditional_true != 19.25 ||
        conditional_false != 24.25)
        return 4;
    return 0;
}

// Atomic float-complex load/store and assignment-expression boundaries.
// Expected: exit=0
#include <complex.h>

static _Atomic(float complex) global_value = CMPLXF(1.25f, -2.5f);
static int rhs_evaluations;

static int check_value(float complex value, float real, float imag) {
    return crealf(value) == real && cimagf(value) == imag;
}

static float complex next_increment(void) {
    rhs_evaluations++;
    return CMPLXF(1.5f, 2.25f);
}

int main(void) {
    _Atomic(float complex) local_value = CMPLXF(3.25f, -4.5f);
    float complex global_snapshot = global_value;
    float complex local_snapshot = local_value;

    if (!check_value(global_snapshot, 1.25f, -2.5f) ||
        !check_value(local_snapshot, 3.25f, -4.5f))
        return 1;

    float complex assignment_result =
        (local_value = CMPLXF(5.25f, -6.5f));
    local_snapshot = local_value;
    if (!check_value(assignment_result, 5.25f, -6.5f) ||
        !check_value(local_snapshot, 5.25f, -6.5f))
        return 2;

    float complex compound_result =
        (local_value += next_increment());
    local_snapshot = local_value;
    if (rhs_evaluations != 1 ||
        !check_value(compound_result, 6.75f, -4.25f) ||
        !check_value(local_snapshot, 6.75f, -4.25f))
        return 3;

    compound_result = (local_value *= 2.0f);
    local_snapshot = local_value;
    if (!check_value(compound_result, 13.5f, -8.5f) ||
        !check_value(local_snapshot, 13.5f, -8.5f))
        return 4;

    global_value = local_value;
    global_snapshot = global_value;
    if (!check_value(global_snapshot, 13.5f, -8.5f))
        return 5;
    return 0;
}

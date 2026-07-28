/*
 * Null pointers and zero-valued complex constants leave a path to the closing
 * brace.  Each function must retain W3005 on both Native and Wasm targets.
 */
#include <complex.h>

static int side_effect_count;

static int next_condition(int value) {
    side_effect_count += 1;
    return value;
}

static int pointer_null_false(void) {
    while ((void *)0) {
    }
}

static int function_pointer_null_false(void) {
    while ((int (*)(void))0) {
    }
}

static int pointer_ternary_false(void) {
    while (1 ? (void *)0 : (void *)1) {
    }
}

static int complex_zero_false(void) {
    while ((double _Complex)0.0) {
    }
}

static int complex_literal_zero_false(void) {
    while (CMPLX(0.0, 0.0)) {
    }
}

static int complex_compare_false(void) {
    while (I == 0.0) {
    }
}

static int complex_bool_cast_false(void) {
    while ((_Bool)CMPLXF(0.0f, 0.0f)) {
    }
}

static int complex_ternary_false(void) {
    while (1 ? CMPLXF(0.0f, 0.0f) : I) {
    }
}

static int side_effect_logical_and_false(void) {
    while (next_condition(1) && 0) {
    }
}

static int side_effect_comma_pointer_false(void) {
    while (!!(next_condition(1), (void *)0)) {
    }
}

static int floating_negative_zero_false(void) {
    while (-0.0) {
    }
}

static int complex_signed_zero_false(void) {
    while (CMPLX(-0.0, 0.0)) {
    }
}

static int complex_partial_initializer_false(void) {
    while ((double _Complex){0.0}) {
    }
}

int main(void) {
    return 0;
}

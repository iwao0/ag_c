/*
 * Zero complex results, observable volatile loads, and runtime-unknown calls
 * leave the closing brace conservatively reachable and must retain W3005.
 */
#include <complex.h>

static int side_effect_count;

static int next_int(int value) {
    side_effect_count += 1;
    return value;
}

static double next_double(double value) {
    side_effect_count += 1;
    return value;
}

static double _Complex next_complex(double _Complex value) {
    side_effect_count += 1;
    return value;
}

static int comma_complex_false(void) {
    while ((next_int(0), (double _Complex)0.0)) {
    }
}

static int cast_complex_false(void) {
    while ((double _Complex)(next_int(0), 0)) {
    }
}

static int unary_complex_false(void) {
    while (-(next_int(0), (double _Complex)0.0)) {
    }
}

static int add_complex_false(void) {
    while ((next_int(0), I) + -I) {
    }
}

static int multiply_complex_false(void) {
    while ((next_int(0), I) * 0.0) {
    }
}

static int selected_complex_false(void) {
    while ((next_int(0), 1)
               ? (double _Complex)0.0
               : (next_int(99), I)) {
    }
}

static int selected_else_complex_false(void) {
    while ((next_int(0), 0)
               ? (next_int(99), I)
               : (double _Complex)0.0) {
    }
}

static int complex_compare_false(void) {
    while ((next_int(0), I) == 0.0) {
    }
}

static int complex_component_false(void) {
    while (__real__ (next_int(0), I)) {
    }
}

static int floating_to_complex_false(void) {
    while ((double _Complex)(next_double(0.0), 0.0)) {
    }
}

static int compound_complex_false(void) {
    while ((double _Complex){(next_double(0.0), 0.0)}) {
    }
}

static int two_component_complex_false(void) {
    while (CMPLX((next_double(0.0), 0.0),
                 (next_double(0.0), 0.0))) {
    }
}

static int volatile_complex_unknown(void) {
    while ((volatile double _Complex){1.0}) {
    }
}

static int runtime_complex_unknown(void) {
    while (next_complex(I)) {
    }
}

int main(void) {
    if ((next_int(0), (double _Complex)0.0)) return 1;
    if (side_effect_count != 1) return 2;
    if ((double _Complex)(next_int(0), 0)) return 3;
    if (side_effect_count != 2) return 4;
    if (-(next_int(0), (double _Complex)0.0)) return 5;
    if (side_effect_count != 3) return 6;
    if ((next_int(0), I) + -I) return 7;
    if (side_effect_count != 4) return 8;
    if ((next_int(0), I) * 0.0) return 9;
    if (side_effect_count != 5) return 10;
    if ((next_int(0), 1)
            ? (double _Complex)0.0
            : (next_int(99), I))
        return 11;
    if (side_effect_count != 6) return 12;
    if ((next_int(0), 0)
            ? (next_int(99), I)
            : (double _Complex)0.0)
        return 13;
    if (side_effect_count != 7) return 14;
    if ((next_int(0), I) == 0.0) return 15;
    if (side_effect_count != 8) return 16;
    if (__real__ (next_int(0), I)) return 17;
    if (side_effect_count != 9) return 18;
    if ((double _Complex)(next_double(0.0), 0.0)) return 19;
    if (side_effect_count != 10) return 20;
    if ((double _Complex){(next_double(0.0), 0.0)}) return 21;
    if (side_effect_count != 11) return 22;
    if (CMPLX((next_double(0.0), 0.0),
              (next_double(0.0), 0.0)))
        return 23;
    if (side_effect_count != 13) return 24;
    if (!(volatile double _Complex){1.0}) return 25;
    if (!next_complex(I)) return 26;
    if (side_effect_count != 14) return 27;
    return 0;
}

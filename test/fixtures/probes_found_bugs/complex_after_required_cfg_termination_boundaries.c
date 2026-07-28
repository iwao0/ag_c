/*
 * A complex value that is known after required side effects can terminate an
 * endless-loop CFG.  The condition still has to execute every required side
 * effect exactly once and must not evaluate an unselected conditional branch.
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

static int comma_complex_true(int value) {
    if (value == 42) return 42;
    while ((next_int(0), I)) {
    }
}

static int cast_complex_true(int value) {
    if (value == 42) return 42;
    while ((double _Complex)(next_int(0), 1)) {
    }
}

static int unary_complex_true(int value) {
    if (value == 42) return 42;
    while (-(next_int(0), I)) {
    }
}

static int add_complex_true(int value) {
    if (value == 42) return 42;
    while ((next_int(0), I) + 1.0) {
    }
}

static int multiply_complex_true(int value) {
    if (value == 42) return 42;
    while ((next_int(0), I) * I) {
    }
}

static int divide_complex_true(int value) {
    if (value == 42) return 42;
    while ((next_int(0), I) / I) {
    }
}

static int selected_complex_true(int value) {
    if (value == 42) return 42;
    while ((next_int(0), 1)
               ? I
               : (next_int(99), (double _Complex)0.0)) {
    }
}

static int selected_else_complex_true(int value) {
    if (value == 42) return 42;
    while ((next_int(0), 0)
               ? (next_int(99), (double _Complex)0.0)
               : I) {
    }
}

static int bool_cast_complex_true(int value) {
    if (value == 42) return 42;
    while ((_Bool)(next_int(0), I)) {
    }
}

static int complex_compare_true(int value) {
    if (value == 42) return 42;
    while ((next_int(0), I) != 0.0) {
    }
}

static int complex_component_true(int value) {
    if (value == 42) return 42;
    while (__imag__ (next_int(0), I)) {
    }
}

static int floating_to_complex_true(int value) {
    if (value == 42) return 42;
    while ((double _Complex)(next_double(0.0), 1.0)) {
    }
}

static int compound_complex_true(int value) {
    if (value == 42) return 42;
    while ((double _Complex){(next_double(0.0), 1.0)}) {
    }
}

static int two_component_complex_true(int value) {
    if (value == 42) return 42;
    while (CMPLX((next_double(0.0), 0.0),
                 (next_double(0.0), 1.0))) {
    }
}

int main(void) {
    if (comma_complex_true(42) != 42) return 1;
    if (cast_complex_true(42) != 42) return 2;
    if (unary_complex_true(42) != 42) return 3;
    if (add_complex_true(42) != 42) return 4;
    if (multiply_complex_true(42) != 42) return 5;
    if (divide_complex_true(42) != 42) return 6;
    if (selected_complex_true(42) != 42) return 7;
    if (selected_else_complex_true(42) != 42) return 8;
    if (bool_cast_complex_true(42) != 42) return 9;
    if (complex_compare_true(42) != 42) return 10;
    if (complex_component_true(42) != 42) return 11;
    if (floating_to_complex_true(42) != 42) return 12;
    if (compound_complex_true(42) != 42) return 13;
    if (two_component_complex_true(42) != 42) return 14;

    if (!(next_int(0), I)) return 15;
    if (side_effect_count != 1) return 16;
    if (!(double _Complex)(next_int(0), 1)) return 17;
    if (side_effect_count != 2) return 18;
    if (!-(next_int(0), I)) return 19;
    if (side_effect_count != 3) return 20;
    if (!((next_int(0), I) + 1.0)) return 21;
    if (side_effect_count != 4) return 22;
    if (!((next_int(0), I) * I)) return 23;
    if (side_effect_count != 5) return 24;
    if (!((next_int(0), I) / I)) return 25;
    if (side_effect_count != 6) return 26;
    if (!((next_int(0), 1)
              ? I
              : (next_int(99), (double _Complex)0.0)))
        return 27;
    if (side_effect_count != 7) return 28;
    if (!((next_int(0), 0)
              ? (next_int(99), (double _Complex)0.0)
              : I))
        return 29;
    if (side_effect_count != 8) return 30;
    if (!(_Bool)(next_int(0), I)) return 31;
    if (side_effect_count != 9) return 32;
    if (!((next_int(0), I) != 0.0)) return 33;
    if (side_effect_count != 10) return 34;
    if (!__imag__ (next_int(0), I)) return 35;
    if (side_effect_count != 11) return 36;
    if (!(double _Complex)(next_double(0.0), 1.0)) return 37;
    if (side_effect_count != 12) return 38;
    if (!(double _Complex){(next_double(0.0), 1.0)}) return 39;
    if (side_effect_count != 13) return 40;
    if (!CMPLX((next_double(0.0), 0.0),
               (next_double(0.0), 1.0)))
        return 41;
    if (side_effect_count != 15) return 42;
    return 0;
}

/*
 * Side-effect-free nonnull addresses and nonzero complex constants terminate
 * a nonvoid function's reachable CFG when they control an endless loop.
 * Calls use an early return so Native, WAT, and object E2E can execute safely.
 */
#include <complex.h>
#include <math.h>

static int object;
static int condition_count;

static int function_target(void) {
    return 1;
}

static int next_condition(int value) {
    condition_count += 1;
    return value;
}

static int pointer_literal_true(int value) {
    if (value == 42) return 42;
    while ((void *)1) {
    }
}

static int object_address_true(int value) {
    if (value == 42) return 42;
    while (&object) {
    }
}

static int string_address_true(int value) {
    if (value == 42) return 42;
    while ("constant") {
    }
}

static int function_address_true(int value) {
    if (value == 42) return 42;
    while (function_target) {
    }
}

static int complex_real_true(int value) {
    if (value == 42) return 42;
    while ((double _Complex)1.0) {
    }
}

static int complex_literal_true(int value) {
    if (value == 42) return 42;
    while (CMPLXF(0.0f, 1.0f)) {
    }
}

static int complex_imaginary_true(int value) {
    if (value == 42) return 42;
    while (I) {
    }
}

static int complex_arithmetic_true(int value) {
    if (value == 42) return 42;
    while (0.0 + 2.0 * I) {
    }
}

static int complex_compare_true(int value) {
    if (value == 42) return 42;
    while (I != 0.0) {
    }
}

static int complex_bool_cast_true(int value) {
    if (value == 42) return 42;
    while ((_Bool)I) {
    }
}

static int complex_component_true(int value) {
    if (value == 42) return 42;
    while (__imag__ I) {
    }
}

static int complex_ternary_true(int value) {
    if (value == 42) return 42;
    while (1 ? I : (float _Complex)0.0f) {
    }
}

static int complex_comma_true(int value) {
    if (value == 42) return 42;
    while (1 ? (0, I) : (float _Complex)0.0f) {
    }
}

static int side_effect_logical_or_true(int value) {
    if (value == 42) return 42;
    while (next_condition(0) || 1) {
    }
}

static int side_effect_comma_pointer_true(int value) {
    if (value == 42) return 42;
    while (!!(next_condition(0), &object)) {
    }
}

static int floating_infinity_true(int value) {
    if (value == 42) return 42;
    while (INFINITY) {
    }
}

static int floating_nan_true(int value) {
    if (value == 42) return 42;
    while (NAN) {
    }
}

static int complex_real_nan_true(int value) {
    if (value == 42) return 42;
    while (CMPLX(NAN, 0.0)) {
    }
}

static int complex_imaginary_infinity_true(int value) {
    if (value == 42) return 42;
    while (CMPLX(0.0, INFINITY)) {
    }
}

static int complex_partial_initializer_true(int value) {
    if (value == 42) return 42;
    while ((double _Complex){1.0}) {
    }
}

static float next_component(int *count, float value) {
    *count += 1;
    return value;
}

static int address_index_count;
static int address_values[2];

static int next_index(void) {
    address_index_count += 1;
    return 0;
}

int main(void) {
    int component_count = 0;

    if (pointer_literal_true(42) != 42) return 1;
    if (object_address_true(42) != 42) return 2;
    if (string_address_true(42) != 42) return 3;
    if (function_address_true(42) != 42) return 4;
    if (complex_real_true(42) != 42) return 5;
    if (complex_literal_true(42) != 42) return 6;
    if (complex_imaginary_true(42) != 42) return 7;
    if (complex_arithmetic_true(42) != 42) return 8;
    if (complex_compare_true(42) != 42) return 9;
    if (complex_bool_cast_true(42) != 42) return 10;
    if (complex_component_true(42) != 42) return 11;
    if (complex_ternary_true(42) != 42) return 12;
    if (complex_comma_true(42) != 42) return 13;
    if (side_effect_logical_or_true(42) != 42) return 14;
    if (side_effect_comma_pointer_true(42) != 42) return 15;

    if (!CMPLXF(next_component(&component_count, 0.0f),
                next_component(&component_count, 1.0f)))
        return 16;
    if (component_count != 2)
        return 17;
    if (CMPLXF(next_component(&component_count, 0.0f),
               next_component(&component_count, 0.0f)))
        return 18;
    if (component_count != 4)
        return 19;

    if (!&address_values[next_index()])
        return 20;
    if (address_index_count != 1)
        return 21;

    if (!(next_condition(0) || 1))
        return 22;
    if (condition_count != 1)
        return 23;
    if (!(next_condition(0), &object))
        return 24;
    if (condition_count != 2)
        return 25;
    while (next_condition(1) && 0)
        return 26;
    if (condition_count != 3)
        return 27;
    while (!!(next_condition(1), (void *)0))
        return 28;
    if (condition_count != 4)
        return 29;
    if (floating_infinity_true(42) != 42)
        return 30;
    if (floating_nan_true(42) != 42)
        return 31;
    if (complex_real_nan_true(42) != 42)
        return 32;
    if (complex_imaginary_infinity_true(42) != 42)
        return 33;
    if (complex_partial_initializer_true(42) != 42)
        return 34;
    return 0;
}

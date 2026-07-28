/*
 * Standard scalar constant forms and nonnull pointer arithmetic terminate an
 * endless-loop CFG.  Initializer and offset side effects must still execute.
 */
#include <stddef.h>

struct pair {
    int first;
    int second;
};

enum truth_value {
    ENUM_TRUE = 1
};

static int object;
static int side_effect_count;

static int next_value(int value) {
    side_effect_count += 1;
    return value;
}

static double next_double(double value) {
    side_effect_count += 1;
    return value;
}

static int sizeof_true(int value) {
    if (value == 42) return 42;
    while (sizeof(int)) {
    }
}

static int alignof_true(int value) {
    if (value == 42) return 42;
    while (_Alignof(int)) {
    }
}

static int enum_true(int value) {
    if (value == 42) return 42;
    while (ENUM_TRUE) {
    }
}

static int generic_true(int value) {
    if (value == 42) return 42;
    while (_Generic((int)0, int: 1, default: 0)) {
    }
}

static int offsetof_true(int value) {
    if (value == 42) return 42;
    while (offsetof(struct pair, second)) {
    }
}

static int string_offset_true(int value) {
    if (value == 42) return 42;
    while ("x" + 1) {
    }
}

static int reversed_string_offset_true(int value) {
    if (value == 42) return 42;
    while (1 + "x") {
    }
}

static int object_offset_true(int value) {
    if (value == 42) return 42;
    while (&object + 1) {
    }
}

static int side_effect_offset_true(int value) {
    if (value == 42) return 42;
    while (&object + (next_value(0), 0)) {
    }
}

static int integer_compound_true(int value) {
    if (value == 42) return 42;
    while ((int){1}) {
    }
}

static int side_effect_compound_true(int value) {
    if (value == 42) return 42;
    while ((int){(next_value(0), 1)}) {
    }
}

static int floating_compound_true(int value) {
    if (value == 42) return 42;
    while ((double){1.0}) {
    }
}

static int side_effect_floating_compound_true(int value) {
    if (value == 42) return 42;
    while ((double){(next_value(0), 1.0)}) {
    }
}

static int pointer_compound_true(int value) {
    if (value == 42) return 42;
    while ((int *){&object}) {
    }
}

static int side_effect_pointer_compound_true(int value) {
    if (value == 42) return 42;
    while ((int *){(next_value(0), &object)}) {
    }
}

static int side_effect_integer_add_true(int value) {
    if (value == 42) return 42;
    while ((next_value(99), 2) + 3) {
    }
}

static int side_effect_integer_shift_true(int value) {
    if (value == 42) return 42;
    while ((next_value(99), 1) << 3) {
    }
}

static int side_effect_integer_compare_true(int value) {
    if (value == 42) return 42;
    while ((next_value(99), 3) == 3) {
    }
}

static int side_effect_integer_cast_true(int value) {
    if (value == 42) return 42;
    while ((unsigned char)(next_value(99), 257)) {
    }
}

static int side_effect_floating_add_true(int value) {
    if (value == 42) return 42;
    while ((next_double(99.0), 0.25) + 0.75) {
    }
}

static int side_effect_floating_compare_true(int value) {
    if (value == 42) return 42;
    while ((next_double(99.0), 1.0) < 2.0) {
    }
}

static int side_effect_floating_to_integer_true(int value) {
    if (value == 42) return 42;
    while ((int)(next_double(99.0), 1.5)) {
    }
}

static int side_effect_integer_selected_true(int value) {
    if (value == 42) return 42;
    while ((next_value(99), 1) ? 7 : 0) {
    }
}

static int side_effect_floating_selected_true(int value) {
    if (value == 42) return 42;
    while ((next_value(99), 1) ? 1.0 : 0.0) {
    }
}

static int side_effect_pointer_selected_true(int value) {
    if (value == 42) return 42;
    while ((next_value(99), 1) ? &object : (int *)0) {
    }
}

static int side_effect_selected_else_true(int value) {
    if (value == 42) return 42;
    while ((next_value(99), 0) ? 0 : 9) {
    }
}

static int side_effect_short_circuit_or_true(int value) {
    if (value == 42) return 42;
    while ((next_value(99), 1) || next_value(0)) {
    }
}

int main(void) {
    if (sizeof_true(42) != 42) return 1;
    if (alignof_true(42) != 42) return 2;
    if (enum_true(42) != 42) return 3;
    if (generic_true(42) != 42) return 4;
    if (offsetof_true(42) != 42) return 5;
    if (string_offset_true(42) != 42) return 6;
    if (reversed_string_offset_true(42) != 42) return 7;
    if (object_offset_true(42) != 42) return 8;
    if (side_effect_offset_true(42) != 42) return 9;
    if (integer_compound_true(42) != 42) return 10;
    if (side_effect_compound_true(42) != 42) return 11;
    if (floating_compound_true(42) != 42) return 12;
    if (side_effect_floating_compound_true(42) != 42) return 13;
    if (pointer_compound_true(42) != 42) return 14;
    if (side_effect_pointer_compound_true(42) != 42) return 15;
    if (side_effect_integer_add_true(42) != 42) return 16;
    if (side_effect_integer_shift_true(42) != 42) return 17;
    if (side_effect_integer_compare_true(42) != 42) return 18;
    if (side_effect_integer_cast_true(42) != 42) return 19;
    if (side_effect_floating_add_true(42) != 42) return 20;
    if (side_effect_floating_compare_true(42) != 42) return 21;
    if (side_effect_floating_to_integer_true(42) != 42) return 22;
    if (side_effect_integer_selected_true(42) != 42) return 23;
    if (side_effect_floating_selected_true(42) != 42) return 24;
    if (side_effect_pointer_selected_true(42) != 42) return 25;
    if (side_effect_selected_else_true(42) != 42) return 26;
    if (side_effect_short_circuit_or_true(42) != 42) return 27;

    if (!(&object + (next_value(0), 0))) return 28;
    if (side_effect_count != 1) return 29;
    if (!(int){(next_value(0), 1)}) return 30;
    if (side_effect_count != 2) return 31;
    if (!(double){(next_value(0), 1.0)}) return 32;
    if (side_effect_count != 3) return 33;
    if (!(int *){(next_value(0), &object)}) return 34;
    if (side_effect_count != 4) return 35;
    if (!((next_value(99), 2) + 3)) return 36;
    if (side_effect_count != 5) return 37;
    if (!((next_value(99), 1) << 3)) return 38;
    if (side_effect_count != 6) return 39;
    if (!((next_value(99), 3) == 3)) return 40;
    if (side_effect_count != 7) return 41;
    if (!(unsigned char)(next_value(99), 257)) return 42;
    if (side_effect_count != 8) return 43;
    if (!((next_double(99.0), 0.25) + 0.75)) return 44;
    if (side_effect_count != 9) return 45;
    if (!((next_double(99.0), 1.0) < 2.0)) return 46;
    if (side_effect_count != 10) return 47;
    if (!(int)(next_double(99.0), 1.5)) return 48;
    if (side_effect_count != 11) return 49;
    if (!((next_value(99), 1) ? 7 : 0)) return 50;
    if (side_effect_count != 12) return 51;
    if (!((next_value(99), 1) ? 1.0 : 0.0)) return 52;
    if (side_effect_count != 13) return 53;
    if (!((next_value(99), 1) ? &object : (int *)0)) return 54;
    if (side_effect_count != 14) return 55;
    if (!((next_value(99), 0) ? 0 : 9)) return 56;
    if (side_effect_count != 15) return 57;
    if (!((next_value(99), 1) || next_value(0))) return 58;
    if (side_effect_count != 16) return 59;
    return 0;
}

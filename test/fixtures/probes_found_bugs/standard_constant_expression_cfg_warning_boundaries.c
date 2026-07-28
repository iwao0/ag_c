/*
 * Runtime-unknown pointer offsets and zero scalar compound literals leave the
 * closing brace reachable and must retain W3005.
 */

enum false_value {
    ENUM_FALSE = 0
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

static int pointer_runtime_offset_unknown(int offset) {
    while (&object + offset) {
    }
}

static int enum_false(void) {
    while (ENUM_FALSE) {
    }
}

static int generic_false(void) {
    while (_Generic((int)0, int: 0, default: 1)) {
    }
}

static int integer_compound_false(void) {
    while ((int){0}) {
    }
}

static int floating_compound_false(void) {
    while ((double){0.0}) {
    }
}

static int pointer_compound_false(void) {
    while ((int *){0}) {
    }
}

static int side_effect_integer_sub_false(void) {
    while ((next_value(99), 2) - 2) {
    }
}

static int side_effect_floating_sub_false(void) {
    while ((next_double(99.0), 0.5) - 0.5) {
    }
}

static int side_effect_floating_compare_false(void) {
    while ((next_double(99.0), 2.0) < 1.0) {
    }
}

static int side_effect_integer_selected_false(void) {
    while ((next_value(99), 0) ? 7 : 0) {
    }
}

static int side_effect_floating_selected_false(void) {
    while ((next_value(99), 0) ? 1.0 : 0.0) {
    }
}

static int side_effect_pointer_selected_false(void) {
    while ((next_value(99), 0) ? &object : (int *)0) {
    }
}

static int side_effect_short_circuit_and_false(void) {
    while ((next_value(99), 0) && next_value(1)) {
    }
}

static int volatile_integer_compound_unknown(void) {
    while ((volatile int){1}) {
    }
}

static int volatile_floating_compound_unknown(void) {
    while ((volatile double){1.0}) {
    }
}

static int volatile_pointer_compound_unknown(void) {
    while ((int * volatile){&object}) {
    }
}

int main(void) {
    if ((next_value(99), 2) - 2) return 1;
    if (side_effect_count != 1) return 2;
    if ((next_double(99.0), 0.5) - 0.5) return 3;
    if (side_effect_count != 2) return 4;
    if ((next_double(99.0), 2.0) < 1.0) return 5;
    if (side_effect_count != 3) return 6;
    if ((next_value(99), 0) ? 7 : 0) return 7;
    if (side_effect_count != 4) return 8;
    if ((next_value(99), 0) ? 1.0 : 0.0) return 9;
    if (side_effect_count != 5) return 10;
    if ((next_value(99), 0) ? &object : (int *)0) return 11;
    if (side_effect_count != 6) return 12;
    if ((next_value(99), 0) && next_value(1)) return 13;
    if (side_effect_count != 7) return 14;
    if (!(volatile int){1}) return 15;
    if (!(volatile double){1.0}) return 16;
    if (!(int * volatile){&object}) return 17;
    return 0;
}

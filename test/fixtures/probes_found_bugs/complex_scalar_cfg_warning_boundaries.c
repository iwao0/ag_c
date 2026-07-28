/*
 * Zero converted results and observable/runtime-unknown complex values leave
 * the closing brace reachable and must retain W3005.
 */
#include <complex.h>

static int side_effect_count;

static int next_int(int value) {
    side_effect_count += 1;
    return value;
}

static double _Complex next_complex(double _Complex value) {
    side_effect_count += 1;
    return value;
}

static int complex_to_double_false(void) {
    while ((double)(next_int(0), I)) {
    }
}

static int complex_to_float_false(void) {
    while ((float)(next_int(0), I)) {
    }
}

static int complex_to_int_false(void) {
    while ((int)(next_int(0), I)) {
    }
}

static int complex_to_unsigned_char_false(void) {
    while ((unsigned char)(next_int(0), I)) {
    }
}

static int complex_to_bool_false(void) {
    while ((_Bool)(next_int(0), 0.0 + 0.0 * I)) {
    }
}

static int fractional_complex_to_int_false(void) {
    while ((int)(next_int(0), 0.75 + 2.0 * I)) {
    }
}

static int volatile_complex_to_int_unknown(void) {
    while ((int)(volatile double _Complex){1.0 + 2.0 * I}) {
    }
}

static int runtime_complex_to_int_unknown(void) {
    while ((int)next_complex(1.0 + 2.0 * I)) {
    }
}

int main(void) {
    if ((double)(next_int(0), I)) return 1;
    if (side_effect_count != 1) return 2;
    if ((float)(next_int(0), I)) return 3;
    if (side_effect_count != 2) return 4;
    if ((int)(next_int(0), I)) return 5;
    if (side_effect_count != 3) return 6;
    if ((unsigned char)(next_int(0), I)) return 7;
    if (side_effect_count != 4) return 8;
    if ((_Bool)(next_int(0), 0.0 + 0.0 * I)) return 9;
    if (side_effect_count != 5) return 10;
    if ((int)(next_int(0), 0.75 + 2.0 * I)) return 11;
    if (side_effect_count != 6) return 12;
    if ((int)(volatile double _Complex){1.0 + 2.0 * I} != 1)
        return 13;
    if ((int)next_complex(1.0 + 2.0 * I) != 1) return 14;
    if (side_effect_count != 7) return 15;
    return 0;
}

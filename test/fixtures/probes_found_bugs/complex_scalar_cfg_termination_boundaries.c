/*
 * Complex-to-scalar conversions use both components for _Bool and the real
 * component for real and integer targets.  Required side effects execute once.
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

static int pure_complex_to_int_true(int value) {
    if (value == 42) return 42;
    while ((int)(1.75 + 2.0 * I)) {
    }
}

static int complex_to_double_true(int value) {
    if (value == 42) return 42;
    while ((double)(next_int(0), 1.0 + 2.0 * I)) {
    }
}

static int complex_to_float_true(int value) {
    if (value == 42) return 42;
    while ((float)(next_int(0), 1.0 + 2.0 * I)) {
    }
}

static int complex_to_int_true(int value) {
    if (value == 42) return 42;
    while ((int)(next_int(0), 1.75 + 2.0 * I)) {
    }
}

static int complex_to_negative_int_true(int value) {
    if (value == 42) return 42;
    while ((int)(next_int(0), -1.75 + 2.0 * I)) {
    }
}

static int complex_to_unsigned_char_true(int value) {
    if (value == 42) return 42;
    while ((unsigned char)(next_int(0), 1.75 + 2.0 * I)) {
    }
}

static int complex_to_signed_char_true(int value) {
    if (value == 42) return 42;
    while ((signed char)(next_int(0), -1.75 + 2.0 * I)) {
    }
}

static int complex_to_unsigned_short_true(int value) {
    if (value == 42) return 42;
    while ((unsigned short)(next_int(0), 2.75 + 2.0 * I)) {
    }
}

static int complex_to_bool_true(int value) {
    if (value == 42) return 42;
    while ((_Bool)(next_int(0), 0.0 + 2.0 * I)) {
    }
}

static int compound_complex_to_int_true(int value) {
    if (value == 42) return 42;
    while ((int)CMPLX((next_double(0.0), 1.75),
                      (next_double(0.0), 2.0))) {
    }
}

static int selected_complex_to_int_true(int value) {
    if (value == 42) return 42;
    while ((int)((next_int(0), 1)
                     ? 1.75 + 2.0 * I
                     : (next_int(99), 0.0 + 0.0 * I))) {
    }
}

int main(void) {
    if (pure_complex_to_int_true(42) != 42) return 1;
    if (complex_to_double_true(42) != 42) return 2;
    if (complex_to_float_true(42) != 42) return 3;
    if (complex_to_int_true(42) != 42) return 4;
    if (complex_to_negative_int_true(42) != 42) return 5;
    if (complex_to_unsigned_char_true(42) != 42) return 6;
    if (complex_to_signed_char_true(42) != 42) return 7;
    if (complex_to_unsigned_short_true(42) != 42) return 8;
    if (complex_to_bool_true(42) != 42) return 9;
    if (compound_complex_to_int_true(42) != 42) return 10;
    if (selected_complex_to_int_true(42) != 42) return 11;

    if ((int)(1.75 + 2.0 * I) != 1) return 12;
    if ((double)(next_int(0), 1.0 + 2.0 * I) != 1.0)
        return 13;
    if (side_effect_count != 1) return 14;
    if ((float)(next_int(0), 1.0 + 2.0 * I) != 1.0f)
        return 15;
    if (side_effect_count != 2) return 16;
    if ((int)(next_int(0), 1.75 + 2.0 * I) != 1)
        return 17;
    if (side_effect_count != 3) return 18;
    if ((int)(next_int(0), -1.75 + 2.0 * I) != -1)
        return 19;
    if (side_effect_count != 4) return 20;
    if ((unsigned char)(next_int(0), 1.75 + 2.0 * I) != 1)
        return 21;
    if (side_effect_count != 5) return 22;
    if ((signed char)(next_int(0), -1.75 + 2.0 * I) != -1)
        return 23;
    if (side_effect_count != 6) return 24;
    if ((unsigned short)(next_int(0), 2.75 + 2.0 * I) != 2)
        return 25;
    if (side_effect_count != 7) return 26;
    if (!(_Bool)(next_int(0), 0.0 + 2.0 * I))
        return 27;
    if (side_effect_count != 8) return 28;
    if ((int)CMPLX((next_double(0.0), 1.75),
                   (next_double(0.0), 2.0)) != 1)
        return 29;
    if (side_effect_count != 10) return 30;
    if ((int)((next_int(0), 1)
                  ? 1.75 + 2.0 * I
                  : (next_int(99), 0.0 + 0.0 * I)) != 1)
        return 31;
    if (side_effect_count != 11) return 32;
    return 0;
}

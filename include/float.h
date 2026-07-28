#ifndef _FLOAT_H
#define _FLOAT_H

/* Radix of exponent representation */
#define FLT_RADIX      2

/* Number of decimal digits of precision */
#define FLT_DIG        6
#define DBL_DIG        15
#define LDBL_DIG       15

/* Number of base-FLT_RADIX digits in the significand */
#define FLT_MANT_DIG   24
#define DBL_MANT_DIG   53
#define LDBL_MANT_DIG  53

/* Minimum negative integer such that FLT_RADIX raised to that power minus 1
   is a normalized floating-point number */
#define FLT_MIN_EXP    (-125)
#define DBL_MIN_EXP    (-1021)
#define LDBL_MIN_EXP   (-1021)

/* Minimum negative integer such that 10 raised to that power is in the range
   of normalized floating-point numbers */
#define FLT_MIN_10_EXP (-37)
#define DBL_MIN_10_EXP (-307)
#define LDBL_MIN_10_EXP (-307)

/* Maximum integer such that FLT_RADIX raised to that power minus 1
   is a representable finite floating-point number */
#define FLT_MAX_EXP    128
#define DBL_MAX_EXP    1024
#define LDBL_MAX_EXP   1024

/* Maximum integer such that 10 raised to that power is in the range
   of representable finite floating-point numbers */
#define FLT_MAX_10_EXP 38
#define DBL_MAX_10_EXP 308
#define LDBL_MAX_10_EXP 308

/* Maximum representable finite floating-point number */
#define FLT_MAX        3.40282347e+38F
#define DBL_MAX        1.7976931348623157e+308
#define LDBL_MAX       1.7976931348623157e+308L

/* Minimum normalized positive floating-point number */
#define FLT_MIN        1.17549435e-38F
#define DBL_MIN        2.2250738585072014e-308
#define LDBL_MIN       2.2250738585072014e-308L

/* Difference between 1 and the least value greater than 1 */
#define FLT_EPSILON    1.19209290e-07F
#define DBL_EPSILON    2.2204460492503131e-16
#define LDBL_EPSILON   2.2204460492503131e-16L

/*
 * Current rounding direction for floating-point addition.
 *
 * Apple ARM64 and the ag_c Wasm runtime use the same two-bit mode field:
 *   0 = nearest, 1 = upward, 2 = downward, 3 = toward zero.
 * C's FLT_ROUNDS encoding rotates those values to 1, 2, 3, 0.
 * Keep the query dynamic so fesetround() is reflected immediately.
 */
int fegetround(void);
static inline int __agc_flt_rounds(void) {
    int mode = fegetround();
    if (mode < 0) return -1;
    return (((mode >> 22) + 1) & 3);
}
#define FLT_ROUNDS     (__agc_flt_rounds())

/* Number of decimal digits needed to distinguish any two float/double values */
#define FLT_DECIMAL_DIG  9
#define DBL_DECIMAL_DIG  17
#define LDBL_DECIMAL_DIG 17
#define DECIMAL_DIG      LDBL_DECIMAL_DIG

/* Whether rounding mode for float expressions is non-constant */
#define FLT_EVAL_METHOD  0

/* Whether subnormals are supported */
#define FLT_HAS_SUBNORM  1
#define DBL_HAS_SUBNORM  1
#define LDBL_HAS_SUBNORM 1

/* Minimum positive subnormal floating-point number */
#define FLT_TRUE_MIN   1.40129846e-45F
#define DBL_TRUE_MIN   4.9406564584124654e-324
#define LDBL_TRUE_MIN  4.9406564584124654e-324L

#endif

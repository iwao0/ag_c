/*
 * Preserve the C11 <float.h> contracts for the binary32 and binary64 types
 * used by both targets: macro types, integer constant use, static
 * initialization, normal/subnormal boundaries, and precision relationships.
 */
#include <assert.h>
#include <float.h>

#if FLT_RADIX != 2 || FLT_EVAL_METHOD != 0
#error "unexpected floating evaluation model"
#endif

#if FLT_MANT_DIG != 24 || FLT_DIG != 6 || \
    FLT_MIN_EXP != -125 || FLT_MAX_EXP != 128 || \
    FLT_MIN_10_EXP != -37 || FLT_MAX_10_EXP != 38 || \
    FLT_DECIMAL_DIG != 9 || FLT_HAS_SUBNORM != 1
#error "unexpected float model"
#endif

#if DBL_MANT_DIG != 53 || DBL_DIG != 15 || \
    DBL_MIN_EXP != -1021 || DBL_MAX_EXP != 1024 || \
    DBL_MIN_10_EXP != -307 || DBL_MAX_10_EXP != 308 || \
    DBL_DECIMAL_DIG != 17 || DBL_HAS_SUBNORM != 1
#error "unexpected double model"
#endif

#define IS_FLOAT(expression) \
  _Generic((expression), float: 1, default: 0)
#define IS_DOUBLE(expression) \
  _Generic((expression), double: 1, default: 0)
#define IS_INT(expression) \
  _Generic((expression), int: 1, default: 0)

_Static_assert(IS_FLOAT(FLT_MAX), "FLT_MAX type");
_Static_assert(IS_FLOAT(FLT_MIN), "FLT_MIN type");
_Static_assert(IS_FLOAT(FLT_EPSILON), "FLT_EPSILON type");
_Static_assert(IS_FLOAT(FLT_TRUE_MIN), "FLT_TRUE_MIN type");
_Static_assert(IS_DOUBLE(DBL_MAX), "DBL_MAX type");
_Static_assert(IS_DOUBLE(DBL_MIN), "DBL_MIN type");
_Static_assert(IS_DOUBLE(DBL_EPSILON), "DBL_EPSILON type");
_Static_assert(IS_DOUBLE(DBL_TRUE_MIN), "DBL_TRUE_MIN type");
_Static_assert(IS_INT(FLT_RADIX), "FLT_RADIX type");
_Static_assert(IS_INT(FLT_ROUNDS), "FLT_ROUNDS type");
_Static_assert(IS_INT(FLT_EVAL_METHOD), "FLT_EVAL_METHOD type");
_Static_assert(IS_INT(FLT_MANT_DIG), "FLT_MANT_DIG type");
_Static_assert(IS_INT(DBL_MANT_DIG), "DBL_MANT_DIG type");

static float global_float_values[] = {
    FLT_MAX, FLT_MIN, FLT_EPSILON, FLT_TRUE_MIN,
};
static double global_double_values[] = {
    DBL_MAX, DBL_MIN, DBL_EPSILON, DBL_TRUE_MIN,
};

static int check_float_boundaries(void) {
  volatile float one = 1.0F;
  volatile float epsilon = FLT_EPSILON;
  volatile float minimum = FLT_MIN;
  volatile float true_minimum = FLT_TRUE_MIN;
  volatile float maximum = FLT_MAX;
  volatile float next_subnormal = true_minimum + true_minimum;
  volatile float below_true_minimum = true_minimum / 2.0F;

  assert(maximum > one);
  assert(minimum > 0.0F);
  assert(true_minimum > 0.0F);
  assert(true_minimum < minimum);
  assert(one + epsilon != one);
  assert(next_subnormal > true_minimum);
  assert(below_true_minimum == 0.0F);
  assert(global_float_values[0] == maximum);
  assert(global_float_values[1] == minimum);
  assert(global_float_values[2] == epsilon);
  assert(global_float_values[3] == true_minimum);
  return 0;
}

static int check_double_boundaries(void) {
  volatile double one = 1.0;
  volatile double epsilon = DBL_EPSILON;
  volatile double minimum = DBL_MIN;
  volatile double true_minimum = DBL_TRUE_MIN;
  volatile double maximum = DBL_MAX;
  volatile double next_subnormal = true_minimum + true_minimum;
  volatile double below_true_minimum = true_minimum / 2.0;

  assert(maximum > one);
  assert(minimum > 0.0);
  assert(true_minimum > 0.0);
  assert(true_minimum < minimum);
  assert(one + epsilon != one);
  assert(next_subnormal > true_minimum);
  assert(below_true_minimum == 0.0);
  assert(global_double_values[0] == maximum);
  assert(global_double_values[1] == minimum);
  assert(global_double_values[2] == epsilon);
  assert(global_double_values[3] == true_minimum);
  return 0;
}

int main(void) {
  assert(sizeof(FLT_MAX) == sizeof(float));
  assert(sizeof(DBL_MAX) == sizeof(double));
  assert(FLT_ROUNDS == 1);
  assert(DECIMAL_DIG >= DBL_DECIMAL_DIG);
  assert(check_float_boundaries() == 0);
  assert(check_double_boundaries() == 0);
  return 0;
}

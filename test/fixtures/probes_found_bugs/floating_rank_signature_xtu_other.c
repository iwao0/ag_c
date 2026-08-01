// Definition side of floating_rank_signature_xtu_main.c.
#include <complex.h>

typedef long double floating_rank_callback_t(long double value);
typedef long double _Complex floating_complex_rank_callback_t(
    long double _Complex value);

long double add_floating_rank_values(
    long double left, long double right) {
  return left + right;
}

long double _Complex add_floating_complex_rank_values(
    long double _Complex left, long double _Complex right) {
  return left + right;
}

static long double multiply_floating_rank_value(long double value) {
  return value * 3.0L;
}

static long double _Complex adjust_floating_complex_rank_value(
    long double _Complex value) {
  return value + CMPLXL(0.5L, 0.25L);
}

floating_rank_callback_t *global_floating_rank_multiplier =
    multiply_floating_rank_value;
floating_complex_rank_callback_t *global_floating_complex_rank_adjuster =
    adjust_floating_complex_rank_value;

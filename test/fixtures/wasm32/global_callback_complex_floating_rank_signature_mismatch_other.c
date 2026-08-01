// Incompatible definition: the callback uses double complex even though it
// lowers to the same physical target function type as long double complex.
#include <complex.h>

typedef double _Complex global_double_complex_callback_t(
    double _Complex value);

static double _Complex adjust_complex_floating_rank(double _Complex value) {
  return value + CMPLX(0.25, 0.5);
}

global_double_complex_callback_t *global_complex_floating_rank_callback =
    adjust_complex_floating_rank;

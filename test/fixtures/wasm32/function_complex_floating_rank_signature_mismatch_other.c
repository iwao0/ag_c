// Incompatible definition: the parameter and result use double complex.
// The low-level target ABI remains identical to long double complex here.
#include <complex.h>

double _Complex transform_complex_floating_rank(
    double _Complex value) {
  return value + CMPLX(0.5, -0.25);
}

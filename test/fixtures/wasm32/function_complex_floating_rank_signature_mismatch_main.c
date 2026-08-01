// Complex floating rank remains part of the canonical function signature
// even when double complex and long double complex share the same target ABI.
#include <complex.h>

long double _Complex transform_complex_floating_rank(
    long double _Complex value);

int main(void) {
  long double _Complex result =
      transform_complex_floating_rank(CMPLXL(2.25L, 3.5L));
  return creall(result) == 2.75L && cimagl(result) == 3.25L
             ? 0
             : 1;
}

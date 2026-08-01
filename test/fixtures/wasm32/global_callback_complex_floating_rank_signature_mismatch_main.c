// Function-pointer data metadata recursively preserves complex floating rank.
#include <complex.h>

typedef long double _Complex global_long_double_complex_callback_t(
    long double _Complex value);

extern global_long_double_complex_callback_t
    *global_complex_floating_rank_callback;

int main(void) {
  long double _Complex result =
      global_complex_floating_rank_callback(CMPLXL(4.0L, 1.5L));
  return creall(result) == 4.25L && cimagl(result) == 2.0L
             ? 0
             : 1;
}

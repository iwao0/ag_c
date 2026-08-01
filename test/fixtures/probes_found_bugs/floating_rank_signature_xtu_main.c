// Matching long double function and callback-data signatures remain linkable
// across translation units after floating rank is added to canonical metadata.
// Expected: exit=0.
#include <complex.h>

typedef long double floating_rank_callback_t(long double value);
typedef long double _Complex floating_complex_rank_callback_t(
    long double _Complex value);

long double add_floating_rank_values(
    long double left, long double right);
long double _Complex add_floating_complex_rank_values(
    long double _Complex left, long double _Complex right);
extern floating_rank_callback_t *global_floating_rank_multiplier;
extern floating_complex_rank_callback_t
    *global_floating_complex_rank_adjuster;

int main(void) {
  long double direct =
      add_floating_rank_values(1.25L, 2.5L);
  long double indirect =
      global_floating_rank_multiplier(direct);
  if (_Generic(direct, long double: 1, default: 0) != 1)
    return 1;
  if (_Generic(indirect, long double: 1, default: 0) != 1)
    return 2;
  if (direct != 3.75L || indirect != 11.25L)
    return 3;

  long double _Complex direct_complex =
      add_floating_complex_rank_values(
          CMPLXL(1.25L, 2.5L), CMPLXL(3.0L, 4.5L));
  long double _Complex indirect_complex =
      global_floating_complex_rank_adjuster(direct_complex);
  if (_Generic(direct_complex,
               long double _Complex: 1, default: 0) != 1)
    return 4;
  if (_Generic(indirect_complex,
               long double _Complex: 1, default: 0) != 1)
    return 5;
  if (creall(direct_complex) != 4.25L ||
      cimagl(direct_complex) != 7.0L)
    return 6;
  if (creall(indirect_complex) != 4.75L ||
      cimagl(indirect_complex) != 7.25L)
    return 7;
  return 0;
}

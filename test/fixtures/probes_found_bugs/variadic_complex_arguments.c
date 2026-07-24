/*
 * Default argument promotions do not widen float _Complex.  A variadic
 * float-complex value therefore occupies one packed 8-byte slot, while a
 * double-complex value occupies two consecutive 8-byte slots.
 */
#include <assert.h>
#include <complex.h>
#include <stdarg.h>

static int check_complex_arguments(int marker, ...) {
  va_list arguments;
  va_start(arguments, marker);
  float _Complex narrow = va_arg(arguments, float _Complex);
  double _Complex wide = va_arg(arguments, double _Complex);
  va_end(arguments);
  return marker == 7 &&
         __real__ narrow == 1.5f && __imag__ narrow == 2.5f &&
         __real__ wide == 3.25 && __imag__ wide == 4.75;
}

int main(void) {
  float _Complex narrow = 1.5f + 2.5f * I;
  double _Complex wide = 3.25 + 4.75 * I;
  assert(check_complex_arguments(7, narrow, wide));
  return 0;
}

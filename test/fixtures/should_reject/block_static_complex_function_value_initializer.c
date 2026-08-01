/* A block-scope static complex object cannot be initialized by a function call. */
#include <complex.h>

double _Complex make_value(void) {
  return 1.0 + 2.0 * I;
}

int main(void) {
  static double _Complex value = make_value();
  return 0;
}

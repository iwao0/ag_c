/* A function call is not a constant initializer for a static complex object. */
#include <complex.h>

double _Complex make_value(void) {
  return 1.0 + 2.0 * I;
}

static double _Complex value = make_value();

int main(void) {
  return 0;
}

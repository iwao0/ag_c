#include <complex.h>

int main(void) {
  int value = 7;
  double _Complex invalid = (double _Complex)&value;
  return invalid != 0.0;
}

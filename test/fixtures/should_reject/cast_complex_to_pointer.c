#include <complex.h>

int main(void) {
  int *invalid = (int *)(1.0 + 2.0 * I);
  return invalid == 0;
}

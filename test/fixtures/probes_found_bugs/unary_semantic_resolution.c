#include <assert.h>

int main(void) {
  double zero = 0.0;
  double negative_zero = -zero;
  assert(1.0 / negative_zero < 0.0);

  double _Complex value = {3.0, 4.0};
  double _Complex negative = -value;
  assert(__real__ negative == -3.0);
  assert(__imag__ negative == -4.0);

  long integer = 7;
  assert(__real__ integer == 7);
  assert(__imag__ integer == 0);
  assert(sizeof(__imag__ integer) == sizeof(long));
  return 0;
}

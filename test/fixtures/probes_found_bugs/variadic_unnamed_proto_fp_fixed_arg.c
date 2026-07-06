#include <assert.h>
#include <stdarg.h>

int sum_fp(double, long, ...);

int main(void) {
  int r = sum_fp(2.5, 3L, 4.5, 5);
  assert(r == 15);
  return 0;
}

int sum_fp(double first, long second, ...) {
  va_list ap;
  va_start(ap, second);
  double third = va_arg(ap, double);
  int fourth = va_arg(ap, int);
  va_end(ap);
  return (int)(first + second + third + fourth);
}

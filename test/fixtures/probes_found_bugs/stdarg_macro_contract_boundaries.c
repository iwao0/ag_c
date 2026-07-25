// stdarg macro result types and independent va_list traversal.
// Expected: exit=0
#include <stdarg.h>

static int verify_arguments(int count, ...) {
  va_list primary;
  va_list copy;

  if (!_Generic(
          va_start(primary, count),
          int: 0,
          default: 1) ||
      !_Generic(
          va_copy(copy, primary),
          int: 0,
          default: 1) ||
      !_Generic(
          va_end(primary),
          int: 0,
          default: 1))
    return 1;

  va_start(primary, count);
  va_copy(copy, primary);

  int primary_first = va_arg(primary, int);
  int copy_first = va_arg(copy, int);
  double primary_second = va_arg(primary, double);
  double copy_second = va_arg(copy, double);
  long long primary_third = va_arg(primary, long long);
  long long copy_third = va_arg(copy, long long);

  if (primary_first != 17 || copy_first != 17 ||
      primary_second != 23.5 || copy_second != 23.5 ||
      primary_third != 0x123456789LL ||
      copy_third != 0x123456789LL) {
    va_end(copy);
    va_end(primary);
    return 2;
  }

  if (!_Generic(va_arg(primary, int), int: 1, default: 0) ||
      !_Generic(
          va_arg(copy, double),
          double: 1,
          default: 0) ||
      !_Generic(
          va_arg(primary, long long),
          long long: 1,
          default: 0)) {
    va_end(copy);
    va_end(primary);
    return 3;
  }

  va_end(copy);
  va_end(primary);
  return 0;
}

int main(void) {
  return verify_arguments(
      3, 17, 23.5, 0x123456789LL);
}

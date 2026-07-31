#include <stdarg.h>

// va_copy has void type and cannot initialize an object.
int variadic(int last, ...) {
  va_list args;
  va_list copy;
  va_start(args, last);
  int result = va_copy(copy, args);
  va_end(copy);
  va_end(args);
  return result;
}

int main(void) {
  return variadic(0);
}

#include <stdarg.h>

// va_start has void type and cannot initialize an object.
int variadic(int last, ...) {
  va_list args;
  int result = va_start(args, last);
  va_end(args);
  return result;
}

int main(void) {
  return variadic(0);
}

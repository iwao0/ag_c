#include <assert.h>

int main(void) {
  char padding = 1;
  _Alignas(long long) int value = 42;
  assert((long)&value % _Alignof(long long) == 0);
  assert(value == 42);
  return padding - 1;
}

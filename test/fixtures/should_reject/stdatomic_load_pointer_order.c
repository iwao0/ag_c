#include <stdatomic.h>

// A pointer expression cannot be converted to the memory_order enum.
int main(void) {
  atomic_int value = 1;
  int order = 0;
  return atomic_load_explicit(&value, &order);
}

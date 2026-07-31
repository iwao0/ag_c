#include <stdatomic.h>

// atomic_load requires a pointer to an atomic object.
int main(void) {
  int value = 1;
  return atomic_load(&value);
}

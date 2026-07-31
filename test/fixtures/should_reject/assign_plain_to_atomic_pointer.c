/* A plain pointer is not implicitly compatible with a pointer to an atomic object. */
#include <stdatomic.h>

int main(void) {
  int value = 7;
  int *plain_pointer = &value;
  _Atomic int *invalid = plain_pointer;
  return atomic_load(invalid);
}

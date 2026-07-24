#include <stdatomic.h>

int main(void) {
  _Atomic int value = 7;
  _Atomic int *atomic_pointer = &value;
  int *invalid = atomic_pointer;
  return *invalid;
}

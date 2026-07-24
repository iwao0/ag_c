#include <stdatomic.h>

int main(void) {
  int value = 7;
  int *plain_pointer = &value;
  _Atomic int *invalid = plain_pointer;
  return atomic_load(invalid);
}

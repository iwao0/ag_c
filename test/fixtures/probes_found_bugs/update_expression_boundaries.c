#include <assert.h>
#include <stdatomic.h>

struct Bits {
  unsigned int value : 3;
};

int main(void) {
  double floating = 1.5;
  ++floating;
  assert(floating == 2.5);

  int values[3] = {3, 5, 7};
  int *pointer = &values[1];
  --pointer;
  assert(*pointer == 3);
  pointer += 2;
  assert(*pointer == 7);

  struct Bits bits = {1};
  bits.value++;
  assert(bits.value == 2);

  _Atomic int atomic_value = 1;
  ++atomic_value;
  assert(atomic_value == 2);

  unsigned int integer = 3;
  integer <<= 2;
  integer |= 1;
  integer ^= 4;
  integer &= 13;
  integer %= 7;
  assert(integer == 2);
  return 0;
}

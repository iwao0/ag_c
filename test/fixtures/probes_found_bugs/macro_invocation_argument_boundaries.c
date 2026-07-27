#include <assert.h>

#define ZERO() 42
#define EMPTY_ONE(value) (42 value)
#define PAIR(left, right) ((left) + (right))
#define PICK_SECOND(first, second) (second)

int ZERO = 1;
int nested_marker;

int main(void) {
  assert(ZERO + ZERO /* comments may separate the call */ () == 43);
  assert(EMPTY_ONE() == 42);
  assert(PAIR((nested_marker = 1, 2), 40) == 42);
  assert(nested_marker == 1);
  assert(PICK_SECOND(, 42) == 42);
  return 0;
}

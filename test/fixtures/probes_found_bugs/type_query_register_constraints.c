#include <assert.h>

struct opaque;

struct pair {
  int first;
  int second;
};

static int increment(register int value) {
  return value + 1;
}

int main(void) {
  int value = 7;
  register int *pointer = &value;
  register struct pair pair = {3, 4};
  int length = 3;
  int variable_array[length];
  int sizeof_bound = 2;
  int alignof_side_effect = 0;

  assert(sizeof(struct opaque *) == sizeof(void *));
  assert(_Alignof(struct opaque *) == _Alignof(void *));
  assert(sizeof(variable_array) == 3 * sizeof(int));
  assert(sizeof(int[sizeof_bound++]) == 2 * sizeof(int));
  assert(sizeof_bound == 3);
  assert(_Alignof(int[length]) == _Alignof(int));
  assert(_Alignof(int[(alignof_side_effect++, length)]) == _Alignof(int));
  assert(alignof_side_effect == 0);
  assert(sizeof(void) == 1); /* Deliberate ag_c extension. */
  assert(*pointer == 7);
  assert(&pointer[0] == &value);
  assert(pair.first + pair.second == 7);
  assert(increment(8) == 9);
  return 0;
}

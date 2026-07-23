#include <assert.h>

typedef int function_type(void);
typedef int array_type[2];
typedef int *int_pointer;
typedef _Atomic int atomic_int;
typedef const int const_int;

struct pair {
  int left;
  int right;
};

struct qualified_bits {
  const unsigned int left : 3;
  volatile unsigned int right : 4;
};

static int answer(void) {
  return 42;
}

int main(void) {
  int value = 42;
  array_type values = {20, 22};
  _Atomic int_pointer atomic_pointer = &value;
  function_type * _Atomic atomic_callback = answer;
  array_type * _Atomic atomic_array_pointer = &values;
  _Atomic atomic_int duplicate_atomic = 7;
  _Atomic const_int atomic_const = 8;
  _Atomic struct pair atomic_pair;
  struct qualified_bits qualified_bits = {5, 9};

  assert(*atomic_pointer == 42);
  assert(atomic_callback() == 42);
  assert((*atomic_array_pointer)[0] +
             (*atomic_array_pointer)[1] == 42);
  assert(duplicate_atomic == 7);
  assert(atomic_const == 8);
  assert(sizeof(atomic_pair) >= sizeof(struct pair));
  assert(qualified_bits.left + qualified_bits.right == 14);
  return 0;
}

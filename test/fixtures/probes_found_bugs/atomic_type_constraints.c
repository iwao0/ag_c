#include <assert.h>

struct pair {
  int left;
  int right;
};

/* A qualifier inside a parameter's [] is applied to the adjusted pointer.
 * _Atomic therefore remains part of this callback's parameter type. */
typedef int atomic_array_callback_t(int values[_Atomic 1]);

static int add_one(int value) {
  return value + 1;
}

static _Atomic int atomic_result(void) {
  return 7;
}

static int read_atomic_bound(int values[_Atomic 1]) {
  return values[0];
}

static int read_static_atomic_bound(int values[static _Atomic 1]) {
  return values[0];
}

static int read_atomic_unspecified(int values[_Atomic]) {
  return values[0];
}

static int bump_register_atomic(register _Atomic int value) {
  value += 2;
  return ++value;
}

int main(void) {
  int value = 41;
  int * _Atomic atomic_pointer = &value;
  _Atomic int atomic_value = 41;
  _Atomic int * _Atomic atomic_to_atomic = &atomic_value;
  const int const_value = 9;
  _Atomic(const int *) atomic_const_pointer = &const_value;
  _Atomic(struct pair) atomic_pair;
  _Atomic(int (*)(int)) atomic_callback = add_one;
  int values[1] = {13};
  _Atomic int result = atomic_result();
  atomic_array_callback_t *atomic_array_callback =
      read_atomic_bound;

  assert(*atomic_pointer == 41);
  assert(*atomic_to_atomic == 41);
  assert(*atomic_const_pointer == 9);
  assert(sizeof(atomic_pair) == sizeof(struct pair));
  assert(atomic_callback(41) == 42);
  assert(read_atomic_bound(values) == 13);
  assert(atomic_array_callback(values) == 13);
  assert(read_static_atomic_bound(values) == 13);
  assert(read_atomic_unspecified(values) == 13);
  assert(bump_register_atomic(39) == 42);
  assert(result == 7);
  assert(_Generic(1, _Atomic(int): 1, default: 2) == 2);
  assert(_Generic(atomic_value,
                  _Atomic(int): 1,
                  int: 2,
                  default: 3) == 2);
  return 0;
}

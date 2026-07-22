#include <assert.h>

struct incomplete;

struct complete {
  int value;
};

typedef int row3[3];
typedef int unary_fn(int);

static int add_one(int value) {
  return value + 1;
}

int main(void) {
  int values[3] = {1, 2, 3};
  struct incomplete *incomplete_pointer = 0;
  _Atomic int atomic_value = 7;
  int control_effect = 0;

  assert(_Generic(values,
                  row3: 1,
                  int *: 2,
                  default: 3) == 2);
  assert(_Generic(incomplete_pointer,
                  struct incomplete *: 4,
                  default: 5) == 4);
  assert(_Generic(add_one,
                  unary_fn *: 6,
                  default: 7) == 6);
  assert(_Generic(atomic_value,
                  int: 8,
                  _Atomic(int): 9,
                  default: 10) == 8);
  assert(_Generic((control_effect++, 1),
                  int: 11,
                  default: (struct complete){12}.value) == 11);
  assert(control_effect == 0);
  assert(_Generic(1,
                  int: (struct complete){13}.value,
                  default: sizeof(values)) == 13);
  return 0;
}

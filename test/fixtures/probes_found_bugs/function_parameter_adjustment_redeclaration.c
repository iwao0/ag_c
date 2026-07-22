#include <assert.h>

typedef int Unary(int);

int apply(int callback(int), int value);
int apply(int (*callback)(int), int value) {
  return callback(value);
}

int apply_typedef(Unary callback, int value);
int apply_typedef(Unary *callback, int value) {
  return callback(value);
}

static int increment(int value) { return value + 1; }
static int double_value(int value) { return value * 2; }

int main(void) {
  assert(apply(increment, 41) == 42);
  assert(apply_typedef(double_value, 21) == 42);
  return 0;
}

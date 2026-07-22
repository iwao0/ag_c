#include <assert.h>

static int identity(int identity) { return identity; }

static int add_bias(int add_bias, int value) {
  return add_bias + value;
}

int main(void) {
  assert(identity(42) == 42);
  assert(add_bias(12, 30) == 42);
  return 0;
}

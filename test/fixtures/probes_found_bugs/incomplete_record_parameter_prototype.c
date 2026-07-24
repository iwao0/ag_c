#include <assert.h>

struct incomplete;

static int read_value(struct incomplete value);

struct incomplete {
  int value;
};

static int read_value(struct incomplete value) {
  return value.value;
}

int main(void) {
  struct incomplete value = {42};
  assert(read_value(value) == 42);
  return 0;
}

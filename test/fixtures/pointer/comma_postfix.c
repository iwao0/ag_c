#include <assert.h>

static int add_one(int value) {
  return value + 1;
}

int main(void) {
  int evaluations = 0;
  int values[2] = {3, 4};
  int (*function)(int) = add_one;

  assert((evaluations++, values)[1] == 4);
  assert((evaluations++, function)(4) == 5);
  assert(evaluations == 2);
  return 0;
}

#include <assert.h>

/* C11 6.7.6.3 adjusts both declarations to a pointer parameter. The const
 * qualifier inside [] applies to that adjusted pointer and is top-level for
 * function-type compatibility. */
static int sum_three(int values[const static 3]);

static int sum_three(int values[static const 3]) {
  return values[0] + values[1] + values[2];
}

int main(void) {
  int values[3] = {10, 20, 12};
  assert(sum_three(values) == 42);
  return 0;
}

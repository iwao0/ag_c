#include <assert.h>

static int add_one(int value) { return value + 1; }
static int double_value(int value) { return value * 2; }

int main(void) {
  int select_add = 0;
  int evaluations = 0;

  /* Function designators decay after the conditional/comma operator has
   * selected its result, and the resulting expression remains callable. */
  assert((select_add ? add_one : double_value)(21) == 42);
  select_add = 1;
  assert((select_add ? add_one : double_value)(41) == 42);
  assert((evaluations++, add_one)(41) == 42);
  assert(evaluations == 1);

  int (*selected)(int) = select_add ? add_one : double_value;
  assert(selected(9) == 10);
  return 0;
}

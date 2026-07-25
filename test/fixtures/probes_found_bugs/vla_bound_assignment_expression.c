#include <assert.h>
#include <stddef.h>

static void check_simple_assignment_bound(void) {
  int extent = 1;
  int values[extent = 4];

  assert(extent == 4);
  assert(sizeof(values) == 4 * sizeof(int));
  for (int i = 0; i < extent; ++i) {
    values[i] = i + 1;
  }
  assert(values[0] == 1);
  assert(values[3] == 4);
}

static void check_compound_and_comma_bounds(void) {
  int extent = 2;
  int compound[extent += 3];

  assert(extent == 5);
  assert(sizeof(compound) == 5 * sizeof(int));
  compound[4] = 17;
  assert(compound[4] == 17);

  int evaluations = 0;
  int comma[(evaluations += 2, evaluations + 1)];

  assert(evaluations == 2);
  assert(sizeof(comma) == 3 * sizeof(int));
  comma[2] = 23;
  assert(comma[2] == 23);
}

static void check_typedef_assignment_bound(void) {
  int extent = 2;
  typedef int captured_row[extent = 6];

  assert(extent == 6);
  captured_row first;
  extent = 3;
  captured_row second;
  assert(sizeof(first) == 6 * sizeof(int));
  assert(sizeof(second) == 6 * sizeof(int));
  first[5] = 29;
  second[5] = 31;
  assert(first[5] + second[5] == 60);
}

int main(void) {
  check_simple_assignment_bound();
  check_compound_and_comma_bounds();
  check_typedef_assignment_bound();
  return 0;
}

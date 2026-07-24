#include <assert.h>

static int enter_before_vla(int n) {
  goto before_vla;
  {
  before_vla:
    ;
    int values[n];
    values[0] = 10;
    values[n - 1] = 32;
    return values[0] + values[n - 1];
  }
}

static int leave_vla_scope(int n) {
  int visited = 0;
outside_vla:
  if (visited) return 42;
  visited = 1;
  {
    int values[n];
    values[0] = 42;
    goto outside_vla;
  }
}

static int jump_within_vla_scope(int n) {
  int values[n];
  values[0] = 20;
  goto inside_vla;
inside_vla:
  values[n - 1] = 22;
  return values[0] + values[n - 1];
}

static int leave_vla_typedef_scope(int n) {
  int visited = 0;
outside_typedef:
  if (visited) return 42;
  visited = 1;
  {
    typedef int row[n];
    assert(sizeof(row) == (unsigned long)(n * (int)sizeof(int)));
    goto outside_typedef;
  }
}

static int enter_fixed_array_scope(void) {
  goto fixed_label;
  {
    int values[2] = {20, 22};
fixed_label:
    values[0] = 20;
    values[1] = 22;
    return values[0] + values[1];
  }
}

static int switch_with_outer_vla(int n, int selector) {
  int values[n];
  values[0] = 40;
  values[n - 1] = 2;
  switch (selector) {
    case 1:
      return values[0] + values[n - 1];
    default:
      return 0;
  }
}

static int switch_vla_after_labels(int n, int selector) {
  switch (selector) {
    case 1:
      ;
    default:
      ;
      int values[n];
      values[0] = 21;
      values[n - 1] = 21;
      return values[0] + values[n - 1];
  }
}

int main(void) {
  assert(enter_before_vla(2) == 42);
  assert(leave_vla_scope(2) == 42);
  assert(jump_within_vla_scope(2) == 42);
  assert(leave_vla_typedef_scope(2) == 42);
  assert(enter_fixed_array_scope() == 42);
  assert(switch_with_outer_vla(2, 1) == 42);
  assert(switch_vla_after_labels(2, 1) == 42);
  return 0;
}

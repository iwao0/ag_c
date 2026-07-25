#include <assert.h>

static int bound_evaluations;

static int note_bound(int extent) {
  ++bound_evaluations;
  return extent;
}

static int sum_with_minimum(
    int extent, const int values[restrict static note_bound(extent)]) {
  int sum = 0;
  for (int i = 0; i < extent; ++i) {
    sum += values[i];
  }
  return sum;
}

static int sum_with_runtime_bound(
    int extent, const int values[note_bound(extent)]) {
  int sum = 0;
  for (int i = 0; i < extent; ++i) {
    sum += values[i];
  }
  return sum;
}

static int overwrite_extent_before_body(
    int extent, const int values[static (extent = 2)]) {
  return extent + values[1];
}

int main(void) {
  int first[4] = {1, 2, 3, 4};
  int second[3] = {5, 6, 7};

  assert(bound_evaluations == 0);
  assert(sum_with_minimum(4, first) == 10);
  assert(bound_evaluations == 1);
  assert(sum_with_minimum(3, second) == 18);
  assert(bound_evaluations == 2);
  assert(sum_with_runtime_bound(4, first) == 10);
  assert(bound_evaluations == 3);
  assert(overwrite_extent_before_body(4, first) == 4);
  return 0;
}

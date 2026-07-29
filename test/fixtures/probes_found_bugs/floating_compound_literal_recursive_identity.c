/*
 * Real floating compound literals use width-specific storage and load/store
 * paths. Each occurrence must reuse its object in one block execution while
 * active recursive frames keep independent objects at every width.
 */
#include <assert.h>

#define FRAME_COUNT 18

static float *active_float[FRAME_COUNT];
static double *active_double[FRAME_COUNT];
static long double *active_long_double[FRAME_COUNT];
static int float_effects;
static int double_effects;
static int long_double_effects;

static float next_float(int depth, int round) {
  float_effects++;
  return (float)(depth * 100 + round * 10 + 1);
}

static double next_double(int depth, int round) {
  double_effects++;
  return (double)(depth * 100 + round * 10 + 2);
}

static long double next_long_double(int depth, int round) {
  long_double_effects++;
  return (long double)(depth * 100 + round * 10 + 3);
}

static void check_values(
    const float *float_value,
    const double *double_value,
    const long double *long_double_value,
    int depth) {
  assert(float_value != 0);
  assert(double_value != 0);
  assert(long_double_value != 0);
  assert(*float_value == (float)(depth * 100 + 11));
  assert(*double_value == (double)(depth * 100 + 12));
  assert(*long_double_value ==
         (long double)(depth * 100 + 13));
}

static void visit_frame(int depth) {
  int round = 0;
  float *first_float = 0;
  double *first_double = 0;
  long double *first_long_double = 0;
  float *float_value = 0;
  double *double_value = 0;
  long double *long_double_value = 0;

repeat_literals:
  float_value =
      &(float){next_float(depth, round)};
  double_value =
      &(double){next_double(depth, round)};
  long_double_value =
      &(long double){next_long_double(depth, round)};
  if (round == 0) {
    first_float = float_value;
    first_double = double_value;
    first_long_double = long_double_value;
    *float_value = -1.0f;
    *double_value = -2.0;
    *long_double_value = -3.0L;
    round = 1;
    goto repeat_literals;
  }

  assert(float_value == first_float);
  assert(double_value == first_double);
  assert(long_double_value == first_long_double);
  check_values(float_value, double_value, long_double_value, depth);

  for (int ancestor = 0; ancestor < depth; ancestor++) {
    assert(float_value != active_float[ancestor]);
    assert(double_value != active_double[ancestor]);
    assert(long_double_value != active_long_double[ancestor]);
    check_values(
        active_float[ancestor],
        active_double[ancestor],
        active_long_double[ancestor],
        ancestor);
  }
  active_float[depth] = float_value;
  active_double[depth] = double_value;
  active_long_double[depth] = long_double_value;

  if (depth + 1 < FRAME_COUNT)
    visit_frame(depth + 1);

  check_values(float_value, double_value, long_double_value, depth);
  for (int ancestor = 0; ancestor < depth; ancestor++)
    check_values(
        active_float[ancestor],
        active_double[ancestor],
        active_long_double[ancestor],
        ancestor);
  active_float[depth] = 0;
  active_double[depth] = 0;
  active_long_double[depth] = 0;
}

int main(void) {
  visit_frame(0);
  assert(float_effects == FRAME_COUNT * 2);
  assert(double_effects == FRAME_COUNT * 2);
  assert(long_double_effects == FRAME_COUNT * 2);
  for (int depth = 0; depth < FRAME_COUNT; depth++) {
    assert(active_float[depth] == 0);
    assert(active_double[depth] == 0);
    assert(active_long_double[depth] == 0);
  }
  return 0;
}

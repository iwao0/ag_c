/*
 * Addressable complex compound literals at each width must reuse their object
 * within one block execution while recursive executions keep simultaneously
 * live objects independent.
 */
#include <assert.h>
#include <complex.h>

#define FRAME_COUNT 16

static float _Complex *active_float[FRAME_COUNT];
static double _Complex *active_double[FRAME_COUNT];
static long double _Complex *active_long_double[FRAME_COUNT];
static int real_effects;
static int imaginary_effects;

static float next_float(int depth, int round, int component) {
  if (component == 0)
    real_effects++;
  else
    imaginary_effects++;
  return (float)(depth * 100 + round * 10 + component);
}

static double next_double(int depth, int round, int component) {
  if (component == 0)
    real_effects++;
  else
    imaginary_effects++;
  return (double)(depth * 100 + round * 10 + component + 2);
}

static long double next_long_double(
    int depth, int round, int component) {
  if (component == 0)
    real_effects++;
  else
    imaginary_effects++;
  return (long double)(depth * 100 + round * 10 + component + 4);
}

static void check_values(
    const float _Complex *float_value,
    const double _Complex *double_value,
    const long double _Complex *long_double_value,
    int depth) {
  assert(float_value != 0);
  assert(double_value != 0);
  assert(long_double_value != 0);
  assert(crealf(*float_value) == (float)(depth * 100 + 10));
  assert(cimagf(*float_value) == (float)(depth * 100 + 11));
  assert(creal(*double_value) == (double)(depth * 100 + 12));
  assert(cimag(*double_value) == (double)(depth * 100 + 13));
  assert(creall(*long_double_value) ==
         (long double)(depth * 100 + 14));
  assert(cimagl(*long_double_value) ==
         (long double)(depth * 100 + 15));
}

static void visit_frame(int depth) {
  int round = 0;
  float _Complex *first_float = 0;
  double _Complex *first_double = 0;
  long double _Complex *first_long_double = 0;
  float _Complex *float_value = 0;
  double _Complex *double_value = 0;
  long double _Complex *long_double_value = 0;

repeat_literals:
  float_value = &(float _Complex){
      next_float(depth, round, 0) +
      next_float(depth, round, 1) * I};
  double_value = &(double _Complex){
      next_double(depth, round, 0) +
      next_double(depth, round, 1) * I};
  long_double_value = &(long double _Complex){
      next_long_double(depth, round, 0) +
      next_long_double(depth, round, 1) * I};
  if (round == 0) {
    first_float = float_value;
    first_double = double_value;
    first_long_double = long_double_value;
    *float_value = -1.0f - 1.0f * I;
    *double_value = -2.0 - 2.0 * I;
    *long_double_value = -3.0L - 3.0L * I;
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
        active_float[ancestor], active_double[ancestor],
        active_long_double[ancestor], ancestor);
  }
  active_float[depth] = float_value;
  active_double[depth] = double_value;
  active_long_double[depth] = long_double_value;

  if (depth + 1 < FRAME_COUNT)
    visit_frame(depth + 1);

  check_values(float_value, double_value, long_double_value, depth);
  for (int ancestor = 0; ancestor < depth; ancestor++)
    check_values(
        active_float[ancestor], active_double[ancestor],
        active_long_double[ancestor], ancestor);
  active_float[depth] = 0;
  active_double[depth] = 0;
  active_long_double[depth] = 0;
}

int main(void) {
  visit_frame(0);
  assert(real_effects == FRAME_COUNT * 2 * 3);
  assert(imaginary_effects == FRAME_COUNT * 2 * 3);
  for (int depth = 0; depth < FRAME_COUNT; depth++) {
    assert(active_float[depth] == 0);
    assert(active_double[depth] == 0);
    assert(active_long_double[depth] == 0);
  }
  return 0;
}

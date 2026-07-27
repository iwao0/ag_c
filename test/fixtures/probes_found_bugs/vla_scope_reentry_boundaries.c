/*
 * Each entry into a block containing a VLA evaluates its bound again and
 * creates a new VLA instance.  This applies to ordinary loop iteration and
 * to a backward goto that first leaves the VLA's scope.
 */
#include <assert.h>
#include <stddef.h>

static int evaluate_bound(int *effects, int value) {
  (*effects)++;
  return value;
}

static void check_backward_goto(void) {
  int effects = 0;
  int pass = 0;
  int requested = 2;

repeat:
  {
    int values[evaluate_bound(&effects, requested)];

    assert(effects == pass + 1);
    assert(sizeof(values) == (size_t)requested * sizeof(int));
    for (int index = 0; index < requested; index++)
      values[index] = requested * 10 + index;
    assert(values[0] == requested * 10);
    assert(values[requested - 1] == requested * 10 + requested - 1);

    pass++;
    if (pass == 1) {
      requested = 5;
      goto repeat;
    }
  }

  assert(effects == 2);
  assert(pass == 2);
}

static void check_loop_reentry(void) {
  int effects = 0;
  int total = 0;

  for (int iteration = 1; iteration <= 4; iteration++) {
    int values[evaluate_bound(&effects, iteration)];

    assert(effects == iteration);
    assert(sizeof(values) == (size_t)iteration * sizeof(int));
    for (int index = 0; index < iteration; index++) {
      values[index] = iteration + index;
      total += values[index];
    }
  }

  assert(effects == 4);
  assert(total == 40);
}

int main(void) {
  check_backward_goto();
  check_loop_reentry();
  return 0;
}

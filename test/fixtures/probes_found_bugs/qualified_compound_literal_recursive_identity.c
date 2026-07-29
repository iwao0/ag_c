/*
 * Qualified scalar compound literals retain their qualifiers while following
 * the same per-block-execution identity rule. Volatile reads keep the storage
 * observable instead of allowing scalar constant folding.
 */
#include <assert.h>

#define FRAME_COUNT 24

static volatile int *active_volatile[FRAME_COUNT];
static const int *active_const[FRAME_COUNT];
static const volatile int *active_const_volatile[FRAME_COUNT];
static int volatile_effects;
static int const_effects;
static int const_volatile_effects;

static int next_volatile(int depth, int round) {
  volatile_effects++;
  return depth * 100 + round * 10 + 1;
}

static int next_const(int depth, int round) {
  const_effects++;
  return depth * 100 + round * 10 + 2;
}

static int next_const_volatile(int depth, int round) {
  const_volatile_effects++;
  return depth * 100 + round * 10 + 3;
}

static void check_values(
    const volatile int *volatile_value,
    const int *const_value,
    const volatile int *const_volatile_value,
    int depth) {
  assert(volatile_value != 0);
  assert(const_value != 0);
  assert(const_volatile_value != 0);
  assert(*volatile_value == depth * 100 + 11);
  assert(*const_value == depth * 100 + 12);
  assert(*const_volatile_value == depth * 100 + 13);
}

static void visit_frame(int depth) {
  int round = 0;
  volatile int *first_volatile = 0;
  const int *first_const = 0;
  const volatile int *first_const_volatile = 0;
  volatile int *volatile_value = 0;
  const int *const_value = 0;
  const volatile int *const_volatile_value = 0;

repeat_literals:
  volatile_value =
      &(volatile int){next_volatile(depth, round)};
  const_value =
      &(const int){next_const(depth, round)};
  const_volatile_value =
      &(const volatile int){next_const_volatile(depth, round)};
  if (round == 0) {
    first_volatile = volatile_value;
    first_const = const_value;
    first_const_volatile = const_volatile_value;
    *volatile_value = -1;
    round = 1;
    goto repeat_literals;
  }

  assert(volatile_value == first_volatile);
  assert(const_value == first_const);
  assert(const_volatile_value == first_const_volatile);
  check_values(
      volatile_value, const_value, const_volatile_value, depth);

  for (int ancestor = 0; ancestor < depth; ancestor++) {
    assert(volatile_value != active_volatile[ancestor]);
    assert(const_value != active_const[ancestor]);
    assert(const_volatile_value != active_const_volatile[ancestor]);
    check_values(
        active_volatile[ancestor], active_const[ancestor],
        active_const_volatile[ancestor], ancestor);
  }
  active_volatile[depth] = volatile_value;
  active_const[depth] = const_value;
  active_const_volatile[depth] = const_volatile_value;

  if (depth + 1 < FRAME_COUNT)
    visit_frame(depth + 1);

  check_values(
      volatile_value, const_value, const_volatile_value, depth);
  for (int ancestor = 0; ancestor < depth; ancestor++)
    check_values(
        active_volatile[ancestor], active_const[ancestor],
        active_const_volatile[ancestor], ancestor);
  active_volatile[depth] = 0;
  active_const[depth] = 0;
  active_const_volatile[depth] = 0;
}

int main(void) {
  visit_frame(0);
  assert(volatile_effects == FRAME_COUNT * 2);
  assert(const_effects == FRAME_COUNT * 2);
  assert(const_volatile_effects == FRAME_COUNT * 2);
  for (int depth = 0; depth < FRAME_COUNT; depth++) {
    assert(active_volatile[depth] == 0);
    assert(active_const[depth] == 0);
    assert(active_const_volatile[depth] == 0);
  }
  return 0;
}

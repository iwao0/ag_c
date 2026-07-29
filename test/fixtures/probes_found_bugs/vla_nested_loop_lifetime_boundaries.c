/*
 * An inner loop's continue and break edges must release only the inner VLA.
 * The outer loop body's VLA remains live until that body finishes, including
 * when loop-target checkpoints are nested for for, while, and do-while.
 */
#include <assert.h>
#include <stddef.h>

#ifdef __wasm32__
#define OUTER_EXTENT_BASE (8 * 1024)
#define INNER_EXTENT_BASE (8 * 1024)
#define OUTER_PASSES 4096
#else
#define OUTER_EXTENT_BASE (1024 * 1024)
#define INNER_EXTENT_BASE (2 * 1024 * 1024)
#define OUTER_PASSES 16
#endif

static int evaluate_bound(int *effects, int value) {
  (*effects)++;
  return value;
}

static void touch(
    volatile unsigned char *bytes, int extent, unsigned char seed) {
  bytes[0] = seed;
  bytes[extent - 1] = (unsigned char)(seed + 1);
  assert(bytes[0] == seed);
  assert(bytes[extent - 1] == (unsigned char)(seed + 1));
}

static void check_outer_guard(
    volatile unsigned char *guard, int extent, unsigned char seed) {
  assert(guard[0] == seed);
  assert(guard[extent - 1] == (unsigned char)(seed + 1));
}

static void check_nested_for(void) {
  int outer_effects = 0;
  int inner_effects = 0;

  for (int outer = 0; outer < OUTER_PASSES; outer++) {
    int guard_extent = OUTER_EXTENT_BASE + (outer & 15);
    volatile unsigned char guard[
        evaluate_bound(&outer_effects, guard_extent)];
    unsigned char guard_seed = (unsigned char)(outer + 7);
    touch(guard, guard_extent, guard_seed);

    int visits = 0;
    for (int inner = 0; inner < 4; inner++) {
      int extent = INNER_EXTENT_BASE + ((outer + inner) & 15);
      volatile unsigned char bytes[
          evaluate_bound(&inner_effects, extent)];
      touch(bytes, extent, (unsigned char)(outer + inner + 17));
      visits++;
      check_outer_guard(guard, guard_extent, guard_seed);
      if (inner < 2)
        continue;
      break;
    }

    assert(visits == 3);
    assert(sizeof(guard) == (size_t)guard_extent);
    check_outer_guard(guard, guard_extent, guard_seed);
  }

  assert(outer_effects == OUTER_PASSES);
  assert(inner_effects == OUTER_PASSES * 3);
}

static void check_nested_while(void) {
  int outer_effects = 0;
  int inner_effects = 0;

  for (int outer = 0; outer < OUTER_PASSES; outer++) {
    int guard_extent = OUTER_EXTENT_BASE + (outer & 15);
    volatile unsigned char guard[
        evaluate_bound(&outer_effects, guard_extent)];
    unsigned char guard_seed = (unsigned char)(outer + 29);
    touch(guard, guard_extent, guard_seed);

    int inner = 0;
    int visits = 0;
    while (inner < 4) {
      int extent = INNER_EXTENT_BASE + ((outer + inner) & 15);
      volatile unsigned char bytes[
          evaluate_bound(&inner_effects, extent)];
      touch(bytes, extent, (unsigned char)(outer + inner + 37));
      visits++;
      inner++;
      check_outer_guard(guard, guard_extent, guard_seed);
      if (inner < 3)
        continue;
      break;
    }

    assert(visits == 3);
    assert(sizeof(guard) == (size_t)guard_extent);
    check_outer_guard(guard, guard_extent, guard_seed);
  }

  assert(outer_effects == OUTER_PASSES);
  assert(inner_effects == OUTER_PASSES * 3);
}

static void check_nested_do_while(void) {
  int outer_effects = 0;
  int inner_effects = 0;

  for (int outer = 0; outer < OUTER_PASSES; outer++) {
    int guard_extent = OUTER_EXTENT_BASE + (outer & 15);
    volatile unsigned char guard[
        evaluate_bound(&outer_effects, guard_extent)];
    unsigned char guard_seed = (unsigned char)(outer + 43);
    touch(guard, guard_extent, guard_seed);

    int inner = 0;
    int visits = 0;
    do {
      int extent = INNER_EXTENT_BASE + ((outer + inner) & 15);
      volatile unsigned char bytes[
          evaluate_bound(&inner_effects, extent)];
      touch(bytes, extent, (unsigned char)(outer + inner + 53));
      visits++;
      inner++;
      check_outer_guard(guard, guard_extent, guard_seed);
      if (inner < 3)
        continue;
      break;
    } while (inner < 4);

    assert(visits == 3);
    assert(sizeof(guard) == (size_t)guard_extent);
    check_outer_guard(guard, guard_extent, guard_seed);
  }

  assert(outer_effects == OUTER_PASSES);
  assert(inner_effects == OUTER_PASSES * 3);
}

int main(void) {
  check_nested_for();
  check_nested_while();
  check_nested_do_while();
  return 0;
}

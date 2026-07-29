/*
 * Nested switches stack loop-body, outer-case, and inner-case VLA lifetimes.
 * Inner break/fallthrough must preserve the outer case, while continue from
 * either switch must release every VLA owned by the loop body.
 */
#include <assert.h>
#include <stddef.h>

#ifdef __wasm32__
#define GUARD_EXTENT_BASE (4 * 1024)
#define OUTER_EXTENT_BASE (8 * 1024)
#define INNER_EXTENT_BASE (8 * 1024)
#define PASSES 4096
#else
#define GUARD_EXTENT_BASE (512 * 1024)
#define OUTER_EXTENT_BASE (512 * 1024)
#define INNER_EXTENT_BASE (512 * 1024)
#define PASSES 16
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

static void check_bytes(
    volatile unsigned char *bytes, int extent, unsigned char seed) {
  assert(bytes[0] == seed);
  assert(bytes[extent - 1] == (unsigned char)(seed + 1));
}

static void check_nested_break_and_fallthrough(void) {
  int guard_effects = 0;
  int outer_effects = 0;
  int inner_effects = 0;

  for (int pass = 0; pass < PASSES; pass++) {
    int guard_extent = GUARD_EXTENT_BASE + (pass & 15);
    volatile unsigned char guard[
        evaluate_bound(&guard_effects, guard_extent)];
    unsigned char guard_seed = (unsigned char)(pass + 3);
    touch(guard, guard_extent, guard_seed);

    int outer_selector = pass % 3;
    int outer_visits = 0;
    int inner_visits = 0;
    switch (outer_selector) {
      case 0: {
        int outer_extent = OUTER_EXTENT_BASE + (pass & 15);
        volatile unsigned char outer_bytes[
            evaluate_bound(&outer_effects, outer_extent)];
        unsigned char outer_seed = (unsigned char)(pass + 17);
        touch(outer_bytes, outer_extent, outer_seed);
        outer_visits++;

        int inner_selector = (pass / 3) % 3;
        switch (inner_selector) {
          case 0: {
            int extent = INNER_EXTENT_BASE + (pass & 15);
            volatile unsigned char inner_bytes[
                evaluate_bound(&inner_effects, extent)];
            touch(inner_bytes, extent, (unsigned char)(pass + 31));
            check_bytes(outer_bytes, outer_extent, outer_seed);
            check_bytes(guard, guard_extent, guard_seed);
            inner_visits++;
          }
          /* fall through */
          case 1: {
            int extent = INNER_EXTENT_BASE + ((pass + 1) & 15);
            volatile unsigned char inner_bytes[
                evaluate_bound(&inner_effects, extent)];
            touch(inner_bytes, extent, (unsigned char)(pass + 41));
            check_bytes(outer_bytes, outer_extent, outer_seed);
            check_bytes(guard, guard_extent, guard_seed);
            inner_visits++;
            break;
          }
          default: {
            int extent = INNER_EXTENT_BASE + ((pass + 2) & 15);
            volatile unsigned char inner_bytes[
                evaluate_bound(&inner_effects, extent)];
            touch(inner_bytes, extent, (unsigned char)(pass + 51));
            check_bytes(outer_bytes, outer_extent, outer_seed);
            check_bytes(guard, guard_extent, guard_seed);
            inner_visits++;
            break;
          }
        }

        check_bytes(outer_bytes, outer_extent, outer_seed);
        check_bytes(guard, guard_extent, guard_seed);
      }
      /* fall through */
      case 1: {
        int extent = OUTER_EXTENT_BASE + ((pass + 1) & 15);
        volatile unsigned char outer_bytes[
            evaluate_bound(&outer_effects, extent)];
        touch(outer_bytes, extent, (unsigned char)(pass + 61));
        check_bytes(guard, guard_extent, guard_seed);
        outer_visits++;
        break;
      }
      default: {
        int extent = OUTER_EXTENT_BASE + ((pass + 2) & 15);
        volatile unsigned char outer_bytes[
            evaluate_bound(&outer_effects, extent)];
        touch(outer_bytes, extent, (unsigned char)(pass + 71));
        check_bytes(guard, guard_extent, guard_seed);
        outer_visits++;
        break;
      }
    }

    assert(outer_visits == (outer_selector == 0 ? 2 : 1));
    if (outer_selector == 0) {
      int inner_selector = (pass / 3) % 3;
      assert(inner_visits == (inner_selector == 0 ? 2 : 1));
    } else {
      assert(inner_visits == 0);
    }
    check_bytes(guard, guard_extent, guard_seed);
  }

  assert(guard_effects == PASSES);
  assert(outer_effects > PASSES);
  assert(inner_effects > 0);
}

static void check_nested_continue(void) {
  int guard_effects = 0;
  int outer_effects = 0;
  int inner_effects = 0;
  int visits = 0;

  for (int pass = 0; pass < PASSES; pass++) {
    int guard_extent = GUARD_EXTENT_BASE + (pass & 15);
    volatile unsigned char guard[
        evaluate_bound(&guard_effects, guard_extent)];
    unsigned char guard_seed = (unsigned char)(pass + 83);
    touch(guard, guard_extent, guard_seed);

    switch (pass & 1) {
      case 0: {
        int outer_extent = OUTER_EXTENT_BASE + (pass & 15);
        volatile unsigned char outer_bytes[
            evaluate_bound(&outer_effects, outer_extent)];
        unsigned char outer_seed = (unsigned char)(pass + 97);
        touch(outer_bytes, outer_extent, outer_seed);

        switch ((pass >> 1) & 1) {
          case 0: {
            int extent = INNER_EXTENT_BASE + (pass & 15);
            volatile unsigned char inner_bytes[
                evaluate_bound(&inner_effects, extent)];
            touch(inner_bytes, extent, (unsigned char)(pass + 107));
            check_bytes(outer_bytes, outer_extent, outer_seed);
            check_bytes(guard, guard_extent, guard_seed);
            visits++;
            continue;
          }
          default: {
            int extent = INNER_EXTENT_BASE + ((pass + 1) & 15);
            volatile unsigned char inner_bytes[
                evaluate_bound(&inner_effects, extent)];
            touch(inner_bytes, extent, (unsigned char)(pass + 117));
            check_bytes(outer_bytes, outer_extent, outer_seed);
            check_bytes(guard, guard_extent, guard_seed);
            visits++;
            continue;
          }
        }
      }
      default: {
        int extent = OUTER_EXTENT_BASE + ((pass + 2) & 15);
        volatile unsigned char outer_bytes[
            evaluate_bound(&outer_effects, extent)];
        touch(outer_bytes, extent, (unsigned char)(pass + 127));
        check_bytes(guard, guard_extent, guard_seed);
        visits++;
        continue;
      }
    }
  }

  assert(visits == PASSES);
  assert(guard_effects == PASSES);
  assert(outer_effects == PASSES);
  assert(inner_effects == (PASSES + 1) / 2);
}

int main(void) {
  check_nested_break_and_fallthrough();
  check_nested_continue();
  return 0;
}

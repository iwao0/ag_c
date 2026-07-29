/*
 * VLA storage owned by either arm of an if statement must be released at the
 * merge, while a VLA outside the conditional remains live. Conditional
 * continue and break edges must also restore the enclosing loop checkpoint.
 */
#include <assert.h>
#include <stddef.h>

#ifdef __wasm32__
#define GUARD_EXTENT_BASE (8 * 1024)
#define BRANCH_EXTENT_BASE (20 * 1024)
#define SIBLING_EXTENT_BASE (20 * 1024)
#define PASSES 4096
#else
#define GUARD_EXTENT_BASE (512 * 1024)
#define BRANCH_EXTENT_BASE (2 * 1024 * 1024)
#define SIBLING_EXTENT_BASE (2 * 1024 * 1024)
#define PASSES 32
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

static void check_normal_merge(void) {
  int guard_effects = 0;
  int branch_effects = 0;
  int sibling_effects = 0;

  for (int pass = 0; pass < PASSES; pass++) {
    int guard_extent = GUARD_EXTENT_BASE + (pass & 15);
    volatile unsigned char guard[
        evaluate_bound(&guard_effects, guard_extent)];
    unsigned char guard_seed = (unsigned char)(pass + 11);
    touch(guard, guard_extent, guard_seed);

    if ((pass & 1) == 0) {
      int extent = BRANCH_EXTENT_BASE + (pass & 15);
      volatile unsigned char branch[
          evaluate_bound(&branch_effects, extent)];
      touch(branch, extent, (unsigned char)(pass + 31));
      check_bytes(guard, guard_extent, guard_seed);
    } else {
      int extent = BRANCH_EXTENT_BASE + ((pass + 1) & 15);
      volatile unsigned char branch[
          evaluate_bound(&branch_effects, extent)];
      touch(branch, extent, (unsigned char)(pass + 51));
      check_bytes(guard, guard_extent, guard_seed);
    }

    check_bytes(guard, guard_extent, guard_seed);
    {
      int extent = SIBLING_EXTENT_BASE + ((pass + 2) & 15);
      volatile unsigned char sibling[
          evaluate_bound(&sibling_effects, extent)];
      touch(sibling, extent, (unsigned char)(pass + 71));
      check_bytes(guard, guard_extent, guard_seed);
    }
    assert(sizeof(guard) == (size_t)guard_extent);
  }

  assert(guard_effects == PASSES);
  assert(branch_effects == PASSES);
  assert(sibling_effects == PASSES);
}

static void check_conditional_loop_exits(void) {
  int guard_effects = 0;
  int branch_effects = 0;
  int visits = 0;

  for (int variant = 0; variant < 2; variant++) {
    for (int pass = 0;; pass++) {
      int guard_extent = GUARD_EXTENT_BASE + (pass & 15);
      volatile unsigned char guard[
          evaluate_bound(&guard_effects, guard_extent)];
      unsigned char guard_seed = (unsigned char)(pass + variant + 91);
      touch(guard, guard_extent, guard_seed);

      if (((pass + variant) & 1) == 0) {
        int extent = BRANCH_EXTENT_BASE + (pass & 15);
        volatile unsigned char branch[
            evaluate_bound(&branch_effects, extent)];
        touch(branch, extent, (unsigned char)(pass + variant + 111));
        check_bytes(guard, guard_extent, guard_seed);
        visits++;
        if (pass + 1 == PASSES)
          break;
        continue;
      } else {
        int extent = BRANCH_EXTENT_BASE + ((pass + 1) & 15);
        volatile unsigned char branch[
            evaluate_bound(&branch_effects, extent)];
        touch(branch, extent, (unsigned char)(pass + variant + 131));
        check_bytes(guard, guard_extent, guard_seed);
        visits++;
        if (pass + 1 == PASSES)
          break;
        continue;
      }
    }
  }

  assert(visits == PASSES * 2);
  assert(guard_effects == PASSES * 2);
  assert(branch_effects == PASSES * 2);
}

int main(void) {
  check_normal_merge();
  check_conditional_loop_exits();
  return 0;
}

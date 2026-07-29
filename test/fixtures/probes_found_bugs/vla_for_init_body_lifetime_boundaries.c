/*
 * A for-init VLA and a body-local VLA form two dynamic-stack lifetimes.
 * Continue releases only the body object, while normal loop exit, break, and
 * goto to an outer scope release both objects.
 */
#include <assert.h>
#include <stddef.h>

#ifdef __wasm32__
#define INIT_EXTENT_BASE (8 * 1024)
#define BODY_EXTENT_BASE (8 * 1024)
#define PASSES 4096
#else
#define INIT_EXTENT_BASE (1024 * 1024)
#define BODY_EXTENT_BASE (2 * 1024 * 1024)
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

static void check_guard(
    volatile unsigned char *guard, int extent, unsigned char seed) {
  assert(guard[0] == seed);
  assert(guard[extent - 1] == (unsigned char)(seed + 1));
  assert(sizeof(*guard) == 1);
}

static void check_continue_and_normal_exit(void) {
  int total_init_effects = 0;
  int total_body_effects = 0;

  for (int pass = 0; pass < PASSES; pass++) {
    int init_effects = 0;
    int body_effects = 0;
    int iteration = 0;
    int guard_extent = INIT_EXTENT_BASE + (pass & 15);
    unsigned char guard_seed = (unsigned char)(pass + 5);

    for (volatile unsigned char guard[
             evaluate_bound(&init_effects, guard_extent)];
         iteration < 4; iteration++) {
      if (iteration == 0)
        touch(guard, guard_extent, guard_seed);
      else
        check_guard(guard, guard_extent, guard_seed);

      int extent = BODY_EXTENT_BASE + ((pass + iteration) & 15);
      volatile unsigned char bytes[
          evaluate_bound(&body_effects, extent)];
      touch(bytes, extent, (unsigned char)(pass + iteration + 17));
      assert(sizeof(bytes) == (size_t)extent);
      assert(sizeof(guard) == (size_t)guard_extent);
      check_guard(guard, guard_extent, guard_seed);
      if (iteration < 3)
        continue;
    }

    assert(init_effects == 1);
    assert(body_effects == 4);
    total_init_effects += init_effects;
    total_body_effects += body_effects;
  }

  assert(total_init_effects == PASSES);
  assert(total_body_effects == PASSES * 4);
}

static void check_break_releases_both_lifetimes(void) {
  int total_init_effects = 0;
  int total_body_effects = 0;

  for (int pass = 0; pass < PASSES; pass++) {
    int init_effects = 0;
    int body_effects = 0;
    int guard_extent = INIT_EXTENT_BASE + (pass & 15);
    unsigned char guard_seed = (unsigned char)(pass + 29);

    for (volatile unsigned char guard[
             evaluate_bound(&init_effects, guard_extent)];;) {
      touch(guard, guard_extent, guard_seed);
      int extent = BODY_EXTENT_BASE + (pass & 15);
      volatile unsigned char bytes[
          evaluate_bound(&body_effects, extent)];
      touch(bytes, extent, (unsigned char)(pass + 37));
      check_guard(guard, guard_extent, guard_seed);
      break;
    }

    assert(init_effects == 1);
    assert(body_effects == 1);
    total_init_effects += init_effects;
    total_body_effects += body_effects;
  }

  assert(total_init_effects == PASSES);
  assert(total_body_effects == PASSES);
}

static void check_goto_releases_both_lifetimes(void) {
  int pass = 0;
  int total_init_effects = 0;
  int total_body_effects = 0;

repeat:
  if (pass == PASSES) {
    assert(total_init_effects == PASSES);
    assert(total_body_effects == PASSES);
    return;
  }

  {
    int init_effects = 0;
    int body_effects = 0;
    int guard_extent = INIT_EXTENT_BASE + (pass & 15);
    unsigned char guard_seed = (unsigned char)(pass + 43);

    for (volatile unsigned char guard[
             evaluate_bound(&init_effects, guard_extent)];;) {
      touch(guard, guard_extent, guard_seed);
      int extent = BODY_EXTENT_BASE + (pass & 15);
      volatile unsigned char bytes[
          evaluate_bound(&body_effects, extent)];
      touch(bytes, extent, (unsigned char)(pass + 53));
      check_guard(guard, guard_extent, guard_seed);
      assert(init_effects == 1);
      assert(body_effects == 1);
      total_init_effects += init_effects;
      total_body_effects += body_effects;
      pass++;
      goto repeat;
    }
  }
}

int main(void) {
  check_continue_and_normal_exit();
  check_break_releases_both_lifetimes();
  check_goto_releases_both_lifetimes();
  return 0;
}

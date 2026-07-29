/*
 * A backward goto may leave one or more inner VLA scopes while remaining
 * inside an outer VLA scope. Inner storage must be released on every edge,
 * while the outer object and its captured bound stay live.
 */
#include <assert.h>
#include <stddef.h>

#ifdef __wasm32__
#define INNER_EXTENT_BASE (8 * 1024)
#define PASSES 64
#else
#define INNER_EXTENT_BASE (2 * 1024 * 1024)
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

static void check_nested_backward_goto(void) {
  int guard_extent = 1024;
  volatile unsigned char guard[guard_extent];
  touch(guard, guard_extent, 0x31);

  int effects = 0;
  int iteration = 0;
repeat:
  {
    int extent = INNER_EXTENT_BASE + (iteration & 15);
    volatile unsigned char bytes[
        evaluate_bound(&effects, extent)];
    touch(bytes, extent, (unsigned char)(iteration + 1));
    assert(guard[0] == 0x31);
    assert(guard[guard_extent - 1] == 0x32);
    iteration++;
    if (iteration < PASSES)
      goto repeat;
  }

  assert(effects == PASSES);
  assert(sizeof(guard) == (size_t)guard_extent);
  assert(guard[0] == 0x31);
  assert(guard[guard_extent - 1] == 0x32);
}

static void check_sibling_scopes_before_backward_goto(void) {
  int guard_extent = 1536;
  volatile unsigned char guard[guard_extent];
  touch(guard, guard_extent, 0x41);

  int effects = 0;
  int iteration = 0;
again:
  {
    int extent = INNER_EXTENT_BASE + (iteration & 15);
    volatile unsigned char first[
        evaluate_bound(&effects, extent)];
    touch(first, extent, (unsigned char)(iteration + 3));
  }
  {
    int extent = INNER_EXTENT_BASE + ((iteration + 7) & 15);
    volatile unsigned char second[
        evaluate_bound(&effects, extent)];
    touch(second, extent, (unsigned char)(iteration + 5));
    assert(guard[0] == 0x41);
    assert(guard[guard_extent - 1] == 0x42);
    iteration++;
    if (iteration < PASSES)
      goto again;
  }

  assert(effects == PASSES * 2);
  assert(sizeof(guard) == (size_t)guard_extent);
  assert(guard[0] == 0x41);
  assert(guard[guard_extent - 1] == 0x42);
}

int main(void) {
  check_nested_backward_goto();
  check_sibling_scopes_before_backward_goto();
  return 0;
}

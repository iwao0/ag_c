/*
 * Falling through from one case-local VLA scope to the next must release the
 * prior case's storage. A VLA outside the switch remains live across every
 * case boundary.
 */
#include <assert.h>
#include <stddef.h>

#ifdef __wasm32__
#define OUTER_EXTENT_BASE (8 * 1024)
#define INNER_EXTENT_BASE (8 * 1024)
#define PASSES 4096
#else
#define OUTER_EXTENT_BASE (512 * 1024)
#define INNER_EXTENT_BASE (512 * 1024)
#define PASSES 16
#endif

#define CASE_COUNT 16

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
}

#define FALLTHROUGH_CASE(number)                                      \
  case number: {                                                      \
    int extent = INNER_EXTENT_BASE + ((pass + number) & 15);          \
    volatile unsigned char bytes[                                    \
        evaluate_bound(&case_effects, extent)];                       \
    touch(bytes, extent, (unsigned char)(pass + number + 19));        \
    assert(sizeof(bytes) == (size_t)extent);                          \
    check_guard(guard, guard_extent, guard_seed);                     \
    visits++;                                                         \
  }

static void check_case_scope_fallthrough(void) {
  int outer_effects = 0;
  int total_case_effects = 0;

  for (int pass = 0; pass < PASSES; pass++) {
    int guard_extent = OUTER_EXTENT_BASE + (pass & 15);
    volatile unsigned char guard[
        evaluate_bound(&outer_effects, guard_extent)];
    unsigned char guard_seed = (unsigned char)(pass + 7);
    touch(guard, guard_extent, guard_seed);

    int selector =
        pass % 3 == 0 ? 0 : (pass % 3 == 1 ? 5 : 15);
    int case_effects = 0;
    int visits = 0;
    switch (selector) {
      FALLTHROUGH_CASE(0)
      /* fall through */
      FALLTHROUGH_CASE(1)
      /* fall through */
      FALLTHROUGH_CASE(2)
      /* fall through */
      FALLTHROUGH_CASE(3)
      /* fall through */
      FALLTHROUGH_CASE(4)
      /* fall through */
      FALLTHROUGH_CASE(5)
      /* fall through */
      FALLTHROUGH_CASE(6)
      /* fall through */
      FALLTHROUGH_CASE(7)
      /* fall through */
      FALLTHROUGH_CASE(8)
      /* fall through */
      FALLTHROUGH_CASE(9)
      /* fall through */
      FALLTHROUGH_CASE(10)
      /* fall through */
      FALLTHROUGH_CASE(11)
      /* fall through */
      FALLTHROUGH_CASE(12)
      /* fall through */
      FALLTHROUGH_CASE(13)
      /* fall through */
      FALLTHROUGH_CASE(14)
      /* fall through */
      FALLTHROUGH_CASE(15)
      break;
      default:
        assert(0);
    }

    assert(visits == CASE_COUNT - selector);
    assert(case_effects == CASE_COUNT - selector);
    assert(sizeof(guard) == (size_t)guard_extent);
    check_guard(guard, guard_extent, guard_seed);
    total_case_effects += case_effects;
  }

  assert(outer_effects == PASSES);
  assert(total_case_effects > PASSES);
}

int main(void) {
  check_case_scope_fallthrough();
  return 0;
}

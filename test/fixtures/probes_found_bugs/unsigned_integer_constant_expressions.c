#include <assert.h>
#include <limits.h>
#include <stdalign.h>

enum constant_values {
  ENUM_UNSIGNED_SHIFT = ~0u >> 1,
  ENUM_UNSIGNED_DIVISION = 1u / -1,
  ENUM_UNSIGNED_MODULO = 1u % -1
};

struct constant_width {
  unsigned value : ((~0u >> 29) == 7 ? 3 : -1);
};

_Alignas((~0u >> 28) == 15 ? 16 : 3) int aligned_value;
int bounded_values[(~0u >> 31) == 1 ? 3 : -1];
static unsigned shifted_value = ~0u >> 1;
static int mixed_comparison = UINT_MAX > -1;
static unsigned aggregate_values[] = {
  ~0u >> 1,
  1u / -1,
  1u % -1
};
static struct {
  unsigned shifted;
  int comparison;
} aggregate_record = {
  ~0u >> 1,
  UINT_MAX > -1
};

_Static_assert((~0u >> 1) == INT_MAX, "unsigned right shift");
_Static_assert((~0ul >> 1) == LONG_MAX, "unsigned long right shift");
_Static_assert((UINT_MAX > -1) == 0, "mixed comparison");
_Static_assert(UINT_MAX == -1, "mixed equality");
_Static_assert((1u / -1) == 0, "mixed division");
_Static_assert((1u % -1) == 1, "mixed modulo");
_Static_assert((1 ? -1 : 0u) == UINT_MAX, "conditional conversion");
_Static_assert((UINT_MAX + 2u) == 1u, "unsigned addition wrap");

static int select_shifted_value(unsigned value) {
  switch (value) {
    case ~0u >> 1:
      return 42;
    default:
      return 0;
  }
}

int main(void) {
  static unsigned local_shifted_value = ~0u >> 1;
  static int local_mixed_comparison = UINT_MAX > -1;

  assert(ENUM_UNSIGNED_SHIFT == INT_MAX);
  assert(ENUM_UNSIGNED_DIVISION == 0);
  assert(ENUM_UNSIGNED_MODULO == 1);
  assert(sizeof(struct constant_width) == sizeof(unsigned));
  assert(((unsigned long)&aligned_value & 15ul) == 0);
  assert(sizeof(bounded_values) == 3 * sizeof(int));
  assert(shifted_value == (unsigned)INT_MAX);
  assert(mixed_comparison == 0);
  assert(aggregate_values[0] == (unsigned)INT_MAX);
  assert(aggregate_values[1] == 0);
  assert(aggregate_values[2] == 1);
  assert(aggregate_record.shifted == (unsigned)INT_MAX);
  assert(aggregate_record.comparison == 0);
  assert(local_shifted_value == (unsigned)INT_MAX);
  assert(local_mixed_comparison == 0);
  assert(select_shifted_value((unsigned)INT_MAX) == 42);
  return 0;
}

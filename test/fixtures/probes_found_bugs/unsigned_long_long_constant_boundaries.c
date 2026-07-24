#include <assert.h>

enum {
  ULL_SHIFT_HIGH = (1ULL << 63) > 0,
  ULL_MIXED_EQUAL = 18446744073709551615ULL == -1LL,
  ULL_MIXED_ORDER = 18446744073709551615ULL > -2LL,
  ULL_DIVISION = 18446744073709551615ULL / 2ULL ==
                 9223372036854775807ULL,
  ULL_MODULO = 18446744073709551615ULL % 2ULL
};

_Static_assert((1ULL << 63) == 9223372036854775808ULL,
               "unsigned long long high bit");
_Static_assert((18446744073709551615ULL + 2ULL) == 1ULL,
               "unsigned long long addition wraps");
_Static_assert((18446744073709551615ULL * 2ULL) ==
                   18446744073709551614ULL,
               "unsigned long long multiplication wraps");
_Static_assert(18446744073709551615ULL == -1LL,
               "mixed equality converts to unsigned long long");
_Static_assert(!(18446744073709551615ULL > -1LL),
               "mixed ordering converts to unsigned long long");

static unsigned long long high_bit = 1ULL << 63;
static unsigned long long wrapped_sum =
    18446744073709551615ULL + 2ULL;
static unsigned long long wrapped_product =
    18446744073709551615ULL * 2ULL;
static int mixed_order = 18446744073709551615ULL > -2LL;
static int selected = 1 ? 17 : -1;

static int classify(unsigned long long value) {
  switch (value) {
    case 1ULL << 63:
      return 1;
    case 18446744073709551615ULL:
      return 2;
    default:
      return 0;
  }
}

int main(void) {
  assert(ULL_SHIFT_HIGH == 1);
  assert(ULL_MIXED_EQUAL == 1);
  assert(ULL_MIXED_ORDER == 1);
  assert(ULL_DIVISION == 1);
  assert(ULL_MODULO == 1);
  assert(high_bit == 9223372036854775808ULL);
  assert(wrapped_sum == 1ULL);
  assert(wrapped_product == 18446744073709551614ULL);
  assert(mixed_order == 1);
  assert(selected == 17);
  assert(classify(9223372036854775808ULL) == 1);
  assert(classify(18446744073709551615ULL) == 2);
  return 0;
}

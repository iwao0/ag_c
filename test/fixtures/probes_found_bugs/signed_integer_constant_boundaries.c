/* Signed integer constant evaluation accepts representable boundary results
 * without relying on host signed overflow. */
#include <assert.h>

enum SignedBoundary {
  SIGNED_MINIMUM = -2147483647 - 1,
  SIGNED_MAXIMUM = 2147483647,
  SIGNED_MINIMUM_PRODUCT = -1073741824 * 2,
  SIGNED_MAXIMUM_SUM = 1073741823 + 1073741824,
  SIGNED_MAXIMUM_SHIFT = 1073741823 << 1,
  SIGNED_MINIMUM_SHIFT = 1 << 31,
  SIGNED_UNSELECTED_TERNARY = 1 ? 9 : 2147483647 + 1,
  SIGNED_UNSELECTED_AND = 0 && (2147483647 + 1),
  SIGNED_UNSELECTED_OR = 1 || (2147483647 + 1),
  SIGNED_MINIMUM_PRODUCT_REVERSED = 2 * -1073741824,
  SIGNED_BOTH_NEGATIVE_PRODUCT = -46340 * -46340
};

static int enum_values[] = {
  SIGNED_MINIMUM,
  SIGNED_MAXIMUM,
  SIGNED_MINIMUM_PRODUCT,
  SIGNED_MAXIMUM_SUM,
  SIGNED_MAXIMUM_SHIFT,
  SIGNED_MINIMUM_SHIFT,
  SIGNED_UNSELECTED_TERNARY,
  SIGNED_UNSELECTED_AND,
  SIGNED_UNSELECTED_OR,
  SIGNED_MINIMUM_PRODUCT_REVERSED,
  SIGNED_BOTH_NEGATIVE_PRODUCT
};
static int minimum = -2147483647 - 1;
static int maximum = 2147483647;
static int product = -1073741824 * 2;
static int shifted = 1073741823 << 1;
static int minimum_shift = 1 << 31;
static long long long_long_minimum =
    -9223372036854775807LL - 1LL;
static long long long_long_minimum_product =
    -4611686018427387904LL * 2LL;
static long long long_long_minimum_shift = 1LL << 63;
static long long long_long_maximum =
    9223372036854775807LL;
static int unselected_ternary =
    1 ? 17 : 2147483647 + 1;
static int unselected_and =
    0 && (2147483647 + 1);
static int unselected_or =
    1 || (2147483647 + 1);

int main(void) {
  assert(enum_values[0] == (-2147483647 - 1));
  assert(enum_values[1] == 2147483647);
  assert(enum_values[2] == (-2147483647 - 1));
  assert(enum_values[3] == 2147483647);
  assert(enum_values[4] == 2147483646);
  assert(enum_values[5] == (-2147483647 - 1));
  assert(minimum == (-2147483647 - 1));
  assert(maximum == 2147483647);
  assert(product == (-2147483647 - 1));
  assert(shifted == 2147483646);
  assert(minimum_shift == (-2147483647 - 1));
  assert(long_long_minimum ==
         (-9223372036854775807LL - 1LL));
  assert(long_long_minimum_product ==
         (-9223372036854775807LL - 1LL));
  assert(long_long_minimum_shift ==
         (-9223372036854775807LL - 1LL));
  assert(long_long_maximum == 9223372036854775807LL);
  assert(enum_values[6] == 9);
  assert(enum_values[7] == 0);
  assert(enum_values[8] == 1);
  assert(enum_values[9] == (-2147483647 - 1));
  assert(enum_values[10] == 2147395600);
  assert(unselected_ternary == 17);
  assert(unselected_and == 0);
  assert(unselected_or == 1);
  return 0;
}

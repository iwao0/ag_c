// Atomic enum lvalue conversion preserves the compatible integer signedness
// through arithmetic, conditional, assignment, and update expressions.
// Expected: exit=0.
#include <limits.h>

enum unsigned_state {
  UNSIGNED_STATE_ZERO = 0,
  UNSIGNED_STATE_VALUE = 25
};

enum signed_state {
  SIGNED_STATE_NEGATIVE = -1,
  SIGNED_STATE_VALUE = 17
};

static _Atomic(enum unsigned_state) unsigned_value =
    UNSIGNED_STATE_VALUE;
static _Atomic(enum signed_state) signed_value =
    SIGNED_STATE_NEGATIVE;

int main(void) {
  int indexed_values[26] = {[25] = 42};
  unsigned int unsigned_quotient = unsigned_value / -2;
  unsigned int unsigned_conditional =
      0 ? unsigned_value : -1;
  double signed_floating = signed_value;
  enum unsigned_state previous_unsigned;
  enum signed_state updated_signed;

  if (unsigned_quotient != 0U)
    return 1;
  if (unsigned_conditional != UINT_MAX)
    return 2;
  if (!(unsigned_value < -1))
    return 3;
  if (signed_value / 2 != 0)
    return 4;
  if (!(signed_value < 0))
    return 5;
  if (signed_floating != -1.0)
    return 6;
  if (indexed_values[unsigned_value] != 42)
    return 7;
  switch (signed_value) {
    case SIGNED_STATE_NEGATIVE:
      break;
    default:
      return 8;
  }

  unsigned_value = (enum unsigned_state)-1;
  if ((unsigned int)unsigned_value != UINT_MAX)
    return 9;
  if ((double)unsigned_value != 4294967295.0 ||
      (long long)unsigned_value != 4294967295LL)
    return 10;
  previous_unsigned = unsigned_value++;
  if ((unsigned int)previous_unsigned != UINT_MAX ||
      unsigned_value != UNSIGNED_STATE_ZERO)
    return 11;

  updated_signed = (signed_value += 2);
  if ((int)updated_signed != 1 || signed_value != 1)
    return 12;
  --signed_value;
  if (signed_value != 0)
    return 13;
  return 0;
}

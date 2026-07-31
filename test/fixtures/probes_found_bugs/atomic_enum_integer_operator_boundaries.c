// Atomic enum lvalue conversion preserves compatible integer signedness for
// integer-only operators and compound atomic read-modify-write expressions.
// Expected: exit=0.

#include <limits.h>

enum unsigned_state {
  UNSIGNED_STATE_ZERO = 0,
  UNSIGNED_STATE_ONE = 1,
  UNSIGNED_STATE_FIVE = 5
};

enum signed_state {
  SIGNED_STATE_NEGATIVE = -17,
  SIGNED_STATE_ZERO = 0,
  SIGNED_STATE_FIVE = 5,
  SIGNED_STATE_SEVENTEEN = 17
};

#define HAS_UNSIGNED_INT_TYPE(expression) \
  _Generic((expression), unsigned int: 1, default: 0)
#define HAS_SIGNED_INT_TYPE(expression) \
  _Generic((expression), int: 1, default: 0)

static _Atomic(enum unsigned_state) unsigned_value =
    UNSIGNED_STATE_FIVE;
static _Atomic(enum signed_state) signed_value =
    SIGNED_STATE_NEGATIVE;
static int unsigned_selector_count;
static int signed_selector_count;

static _Atomic(enum unsigned_state) *select_unsigned(void) {
  unsigned_selector_count++;
  return &unsigned_value;
}

static _Atomic(enum signed_state) *select_signed(void) {
  signed_selector_count++;
  return &signed_value;
}

int main(void) {
  enum unsigned_state unsigned_result;
  enum signed_state signed_result;

  if (!HAS_UNSIGNED_INT_TYPE(+unsigned_value) ||
      !HAS_UNSIGNED_INT_TYPE(~unsigned_value) ||
      !HAS_UNSIGNED_INT_TYPE(unsigned_value % 3) ||
      !HAS_UNSIGNED_INT_TYPE(unsigned_value << 1) ||
      !HAS_UNSIGNED_INT_TYPE(unsigned_value & 3))
    return 1;
  if (!HAS_SIGNED_INT_TYPE(+signed_value) ||
      !HAS_SIGNED_INT_TYPE(~signed_value) ||
      !HAS_SIGNED_INT_TYPE(signed_value % 3) ||
      !HAS_SIGNED_INT_TYPE(signed_value << 1) ||
      !HAS_SIGNED_INT_TYPE(signed_value & 3))
    return 2;

  if (unsigned_value % -2 != 5U)
    return 3;
  if (-unsigned_value != UINT_MAX - 4U)
    return 4;
  if (~unsigned_value != UINT_MAX - 5U)
    return 5;
  if ((unsigned_value << 3) != 40U ||
      (unsigned_value >> 1) != 2U)
    return 6;
  if ((unsigned_value & -1) != 5U ||
      (unsigned_value ^ -1) != UINT_MAX - 5U ||
      (unsigned_value | -2) != UINT_MAX)
    return 7;

  if (signed_value % 5 != -2)
    return 8;
  signed_value = SIGNED_STATE_FIVE;
  if (-signed_value != -5 || ~signed_value != -6)
    return 9;
  if ((signed_value << 3) != 40 ||
      (signed_value >> 1) != 2 ||
      (signed_value & 3) != 1 ||
      (signed_value ^ 3) != 6 ||
      (signed_value | 8) != 13)
    return 10;

  unsigned_value = UNSIGNED_STATE_ONE;
  unsigned_result = (*select_unsigned()) <<= 4;
  if ((unsigned int)unsigned_result != 16U ||
      (unsigned int)unsigned_value != 16U)
    return 11;
  unsigned_result = (*select_unsigned()) |= 3;
  if ((unsigned int)unsigned_result != 19U ||
      (unsigned int)unsigned_value != 19U)
    return 12;
  unsigned_result = (*select_unsigned()) ^= 7;
  if ((unsigned int)unsigned_result != 20U ||
      (unsigned int)unsigned_value != 20U)
    return 13;
  unsigned_result = (*select_unsigned()) >>= 2;
  if ((unsigned int)unsigned_result != 5U ||
      (unsigned int)unsigned_value != 5U)
    return 14;
  unsigned_result = (*select_unsigned()) %= 3;
  if ((unsigned int)unsigned_result != 2U ||
      (unsigned int)unsigned_value != 2U)
    return 15;
  unsigned_result = (*select_unsigned()) &= 3;
  if ((unsigned int)unsigned_result != 2U ||
      (unsigned int)unsigned_value != 2U ||
      unsigned_selector_count != 6)
    return 16;

  signed_value = SIGNED_STATE_SEVENTEEN;
  signed_result = (*select_signed()) %= 5;
  if ((int)signed_result != 2 || (int)signed_value != 2)
    return 17;
  signed_result = (*select_signed()) <<= 2;
  if ((int)signed_result != 8 || (int)signed_value != 8)
    return 18;
  signed_result = (*select_signed()) |= 1;
  if ((int)signed_result != 9 || (int)signed_value != 9)
    return 19;
  signed_result = (*select_signed()) ^= 3;
  if ((int)signed_result != 10 || (int)signed_value != 10)
    return 20;
  signed_result = (*select_signed()) >>= 1;
  if ((int)signed_result != 5 || (int)signed_value != 5)
    return 21;
  signed_result = (*select_signed()) &= 6;
  if ((int)signed_result != 4 || (int)signed_value != 4 ||
      signed_selector_count != 6)
    return 22;

  return 0;
}

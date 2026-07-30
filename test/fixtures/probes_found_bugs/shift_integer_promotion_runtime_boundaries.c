/*
 * Shift operands undergo integer promotion independently. The promoted left
 * operand determines the result width and signedness; the right operand must
 * not force the result through the usual arithmetic conversions.
 */
#include <limits.h>

#define TYPE_IS(expression, type) \
  _Generic((expression), type: 1, default: 0)

_Static_assert(
    TYPE_IS((unsigned char)1 << (unsigned long long)1, int),
    "a wide right operand must not widen the promoted left operand");
_Static_assert(
    TYPE_IS((unsigned int)1 >> (signed char)1, unsigned int),
    "an unsigned int left operand keeps its type");
_Static_assert(
    TYPE_IS((long)1 << (unsigned long long)1, long),
    "a long left operand keeps its target-dependent width");
_Static_assert(
    TYPE_IS((unsigned long)1 >> (long long)1, unsigned long),
    "a signed wide count must not change the left operand signedness");

struct shift_bits {
  signed int signed_value : 6;
  unsigned int narrow_value : 6;
  unsigned int full_value : sizeof(unsigned int) * CHAR_BIT;
  unsigned int count : 5;
};

static int left_calls;
static int right_calls;

static long runtime_signed_long(void) {
  left_calls++;
  return -64L;
}

static unsigned long long runtime_wide_count(void) {
  right_calls++;
  return 2ULL;
}

int main(void) {
  signed char signed_byte = -64;
  unsigned char unsigned_byte = 0x80;
  unsigned short unsigned_half = 0x80;
  unsigned long long wide_count = 2ULL;
  signed char narrow_count = 2;
  _Bool boolean_count = 1;

  if ((signed_byte >> wide_count) != -16)
    return 1;
  if ((unsigned_byte << wide_count) != 512)
    return 2;
  if ((unsigned_half << wide_count) != 512)
    return 3;
  if (sizeof(signed_byte >> wide_count) != sizeof(int))
    return 4;
  if (sizeof(unsigned_half << wide_count) != sizeof(int))
    return 5;

  {
    long signed_long = -64L;
    unsigned long unsigned_long = ULONG_MAX;
    long long signed_wide = -(1LL << 62);

    if ((signed_long >> narrow_count) != -16L)
      return 6;
    if ((unsigned_long >> narrow_count) != ULONG_MAX / 4UL)
      return 7;
    if ((signed_wide >> narrow_count) != -(1LL << 60))
      return 8;
    if ((signed_long >> boolean_count) != -32L)
      return 9;
  }

  {
    struct shift_bits bits = {
        .signed_value = -32,
        .narrow_value = 63,
        .full_value = UINT_MAX,
        .count = 2,
    };

    if ((bits.signed_value >> bits.count) != -8)
      return 10;
    if ((bits.narrow_value << bits.count) != 252)
      return 11;
    if ((bits.full_value >> 31) != 1U)
      return 12;
    if (sizeof(bits.narrow_value << bits.count) != sizeof(int))
      return 13;

    {
      signed char compound_signed = -64;
      unsigned char compound_unsigned = 0x80;
      long compound_long = 3L;
      unsigned long compound_unsigned_long = ULONG_MAX;

      compound_signed >>= wide_count;
      compound_unsigned <<= boolean_count;
      compound_long <<= boolean_count;
      compound_unsigned_long >>= bits.count;

      if (compound_signed != -16)
        return 14;
      if (compound_unsigned != 0)
        return 15;
      if (compound_long != 6L)
        return 16;
      if (compound_unsigned_long != ULONG_MAX / 4UL)
        return 17;
    }
  }

  left_calls = 0;
  right_calls = 0;
  if ((runtime_signed_long() >> runtime_wide_count()) != -16L)
    return 18;
  if (left_calls != 1 || right_calls != 1)
    return 19;

  return 0;
}

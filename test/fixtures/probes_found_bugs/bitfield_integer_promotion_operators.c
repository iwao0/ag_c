// Integer promotions use the width of a bit-field, not only its declared
// integer type. A narrow unsigned int bit-field therefore promotes to int in
// unary and binary operator contexts; a full-width field remains unsigned int.
#include <limits.h>
#include <stdarg.h>

#define TYPE_IS(expression, type) _Generic((expression), type: 1, default: 0)

struct operator_bits {
  unsigned int narrow : 3;
  unsigned int full : sizeof(unsigned int) * CHAR_BIT;
  unsigned long extended_narrow : 3;
};

_Static_assert(
    TYPE_IS(((struct operator_bits){0}).narrow, unsigned int),
    "a bare bit-field keeps its declared type");
_Static_assert(
    TYPE_IS(+((struct operator_bits){0}).narrow, int),
    "unary plus promotes a narrow unsigned bit-field to int");
_Static_assert(
    TYPE_IS(-((struct operator_bits){0}).narrow, int),
    "unary minus promotes a narrow unsigned bit-field to int");
_Static_assert(
    TYPE_IS(~((struct operator_bits){0}).narrow, int),
    "bitwise not promotes a narrow unsigned bit-field to int");
_Static_assert(
    TYPE_IS(((struct operator_bits){0}).narrow + 1, int),
    "usual arithmetic conversions use the promoted bit-field type");
_Static_assert(
    TYPE_IS(((struct operator_bits){0}).narrow << 1, int),
    "shift result uses the promoted left bit-field type");

_Static_assert(
    TYPE_IS(+((struct operator_bits){0}).full, unsigned int),
    "a full-width unsigned bit-field does not promote to int");
_Static_assert(
    TYPE_IS(((struct operator_bits){0}).full + 1, unsigned int),
    "binary arithmetic preserves a full-width unsigned bit-field");
_Static_assert(
    TYPE_IS(((struct operator_bits){0}).extended_narrow, unsigned long),
    "an extended bit-field keeps its declared type when used directly");
_Static_assert(
    TYPE_IS(+((struct operator_bits){0}).extended_narrow, int),
    "an extended narrow bit-field promotes according to its value range");

static int read_promoted_int(int marker, ...) {
  va_list arguments;
  va_start(arguments, marker);
  int value = va_arg(arguments, int);
  va_end(arguments);
  return value;
}

static int select_extended_bitfield(struct operator_bits bits) {
  switch (bits.extended_narrow) {
    case 0:
      return 10;
    case 7:
      return 17;
    default:
      return -1;
  }
}

int main(void) {
  struct operator_bits bits = {
      .narrow = 7,
      .full = UINT_MAX,
      .extended_narrow = 7,
  };

  if (-bits.narrow != -7)
    return 1;
  if (~bits.narrow != ~7)
    return 2;
  if (bits.narrow + -8 != -1)
    return 3;
  if (bits.narrow / -2 != -3)
    return 4;
  if (bits.narrow < -1)
    return 5;
  if (bits.full + 1 != 0)
    return 6;
  if (read_promoted_int(0, bits.extended_narrow) != 7)
    return 7;
  if (select_extended_bitfield(bits) != 17)
    return 8;
  bits.narrow = 7;
  bits.narrow /= -2;
  if (bits.narrow != 5)
    return 9;
  bits.extended_narrow = 7;
  bits.extended_narrow %= -4;
  if (bits.extended_narrow != 3)
    return 10;
  return 0;
}

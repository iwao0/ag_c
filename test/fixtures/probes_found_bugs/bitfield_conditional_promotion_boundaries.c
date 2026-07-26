// Conditional operands that are bit-fields undergo integer promotion before
// their common result type is selected. Narrow unsigned fields therefore
// produce int, while a full-width unsigned field remains unsigned int.
#include <limits.h>

#define TYPE_IS(expression, type) _Generic((expression), type: 1, default: 0)

struct conditional_bits {
  unsigned int unsigned_left : 3;
  unsigned int unsigned_right : 3;
  signed int signed_left : 3;
  signed int signed_right : 3;
  unsigned int full_left : sizeof(unsigned int) * CHAR_BIT;
  unsigned int full_right : sizeof(unsigned int) * CHAR_BIT;
};

_Static_assert(
    TYPE_IS(((struct conditional_bits){0}).unsigned_left, unsigned int),
    "a direct unsigned bit-field keeps its declared type");
_Static_assert(
    TYPE_IS(1 ? ((struct conditional_bits){0}).unsigned_left
              : ((struct conditional_bits){0}).unsigned_right,
            int),
    "narrow unsigned conditional bit-fields promote to int");
_Static_assert(
    TYPE_IS(1 ? ((struct conditional_bits){0}).signed_left
              : ((struct conditional_bits){0}).signed_right,
            int),
    "signed conditional bit-fields promote to int");
_Static_assert(
    TYPE_IS(1 ? ((struct conditional_bits){0}).unsigned_left
              : ((struct conditional_bits){0}).signed_left,
            int),
    "mixed narrow bit-fields share the promoted int type");
_Static_assert(
    TYPE_IS(1 ? ((struct conditional_bits){0}).full_left
              : ((struct conditional_bits){0}).full_right,
            unsigned int),
    "a full-width unsigned bit-field cannot promote to int");

static int select_unsigned_narrow(const struct conditional_bits *bits,
                                  int use_left) {
  return use_left ? bits->unsigned_left : bits->unsigned_right;
}

static int select_signed_narrow(const struct conditional_bits *bits,
                                int use_left) {
  return use_left ? bits->signed_left : bits->signed_right;
}

static int select_mixed_narrow(const struct conditional_bits *bits,
                               int use_unsigned) {
  return use_unsigned ? bits->unsigned_left : bits->signed_left;
}

static unsigned int select_unsigned_full(
    const struct conditional_bits *bits, int use_left) {
  return use_left ? bits->full_left : bits->full_right;
}

int main(void) {
  struct conditional_bits bits = {
      .unsigned_left = 7,
      .unsigned_right = 5,
      .signed_left = -1,
      .signed_right = -3,
      .full_left = UINT_MAX,
      .full_right = UINT_MAX - 1,
  };

  if (select_unsigned_narrow(&bits, 1) != 7 ||
      select_unsigned_narrow(&bits, 0) != 5)
    return 1;
  if (select_signed_narrow(&bits, 1) != -1 ||
      select_signed_narrow(&bits, 0) != -3)
    return 2;
  if (select_mixed_narrow(&bits, 1) != 7 ||
      select_mixed_narrow(&bits, 0) != -1)
    return 3;
  if (select_unsigned_full(&bits, 1) != UINT_MAX ||
      select_unsigned_full(&bits, 0) != UINT_MAX - 1)
    return 4;
  return 0;
}

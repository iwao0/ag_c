// Updating a bit-field stores only the declared width. The value of a prefix
// update or assignment expression must be that normalized stored value, while
// a postfix update still yields the old value. The lvalue is evaluated once.
#include <limits.h>

#define TYPE_IS(expression, type) \
  _Generic((expression), type: 1, default: 0)

struct update_bits {
  unsigned int narrow : 3;
  unsigned int full : sizeof(unsigned int) * CHAR_BIT;
  unsigned long extended : 3;
  signed int signed_narrow : 3;
  _Bool boolean : 1;
};

_Static_assert(
    TYPE_IS(++((struct update_bits *)0)->narrow, unsigned int),
    "prefix increment preserves the declared bit-field type");
_Static_assert(
    TYPE_IS(((struct update_bits *)0)->narrow++, unsigned int),
    "postfix increment preserves the declared bit-field type");
_Static_assert(
    TYPE_IS(++((struct update_bits *)0)->extended, unsigned long),
    "extended bit-field update preserves unsigned long");
_Static_assert(
    TYPE_IS(++((struct update_bits *)0)->signed_narrow, signed int),
    "signed bit-field update preserves signed int");
_Static_assert(
    TYPE_IS(++((struct update_bits *)0)->boolean, _Bool),
    "_Bool bit-field update preserves _Bool");

static int check_prefix_postfix_and_wrap(void) {
  struct update_bits bits = {
      .narrow = 7,
      .full = UINT_MAX,
      .extended = 7,
  };

  unsigned int new_narrow = ++bits.narrow;
  if (new_narrow != 0u || (int)bits.narrow != 0)
    return 1;
  bits.narrow = 7;
  unsigned int old_narrow = bits.narrow++;
  if (old_narrow != 7u || (int)bits.narrow != 0)
    return 2;
  new_narrow = ++bits.narrow;
  if (new_narrow != 1u || (int)bits.narrow != 1)
    return 3;
  old_narrow = bits.narrow--;
  if (old_narrow != 1u || (int)bits.narrow != 0)
    return 4;
  new_narrow = --bits.narrow;
  if (new_narrow != 7u || (int)bits.narrow != 7)
    return 5;

  unsigned int old_full = bits.full++;
  if (old_full != UINT_MAX || bits.full != 0u)
    return 6;

  unsigned long old_extended = bits.extended++;
  if (old_extended != 7ul || (int)bits.extended != 0)
    return 7;
  unsigned long new_extended = --bits.extended;
  if (new_extended != 7ul || (int)bits.extended != 7)
    return 8;
  return 0;
}

static int check_assignment_expression_values(void) {
  struct update_bits bits = {
      .extended = 7,
  };
  unsigned int source = 9;
  unsigned int assigned = (bits.narrow = source);
  if (assigned != 1u || (int)bits.narrow != 1)
    return 1;
  unsigned int compound = (bits.narrow += 8);
  if (compound != 1u || (int)bits.narrow != 1)
    return 2;
  compound = (bits.narrow -= 2);
  if (compound != 7u || (int)bits.narrow != 7)
    return 3;

  unsigned long extended = (bits.extended += 3ul);
  if (extended != 2ul || (int)bits.extended != 2)
    return 4;
  return 0;
}

static int check_single_evaluation(void) {
  struct update_bits values[2] = {
      {
          .narrow = 2,
          .full = 10,
          .extended = 4,
      },
      {
          .narrow = 5,
          .full = 20,
          .extended = 6,
      },
  };
  int index = 0;
  unsigned int old = values[index++].narrow++;
  if (index != 1 || old != 2u || (int)values[0].narrow != 3 ||
      (int)values[1].narrow != 5)
    return 1;

  struct update_bits *pointer = values;
  unsigned long next = (++pointer)->extended++;
  if (pointer != &values[1] || next != 6ul ||
      (int)values[1].extended != 7)
    return 2;
  return 0;
}

static int check_volatile_update(void) {
  volatile struct update_bits bits = {
      .narrow = 3,
      .full = 30,
      .extended = 5,
  };
  unsigned int old = bits.narrow++;
  unsigned long next = ++bits.extended;
  if (old != 3u || (int)bits.narrow != 4 ||
      next != 6ul || (int)bits.extended != 6)
    return 1;
  return 0;
}

static int check_signed_and_boolean_values(void) {
  struct update_bits bits = {
      .signed_narrow = -1,
      .boolean = 0,
  };
  int assigned = (bits.signed_narrow = -2);
  if (assigned != -2 || bits.signed_narrow != -2)
    return 1;
  int compound = (bits.signed_narrow -= 1);
  if (compound != -3 || bits.signed_narrow != -3)
    return 2;
  int prefix = ++bits.signed_narrow;
  if (prefix != -2 || bits.signed_narrow != -2)
    return 3;
  int postfix = bits.signed_narrow++;
  if (postfix != -2 || bits.signed_narrow != -1)
    return 4;

  _Bool bool_prefix = ++bits.boolean;
  if (!bool_prefix || !bits.boolean)
    return 5;
  _Bool bool_postfix = bits.boolean++;
  if (!bool_postfix || !bits.boolean)
    return 6;
  bool_prefix = --bits.boolean;
  if (bool_prefix || bits.boolean)
    return 7;
  bool_prefix = --bits.boolean;
  if (!bool_prefix || !bits.boolean)
    return 8;
  int source = 4;
  _Bool bool_assigned = (bits.boolean = source);
  if (!bool_assigned || !bits.boolean)
    return 9;
  _Bool bool_compound = (bits.boolean += source);
  if (!bool_compound || !bits.boolean)
    return 10;
  return 0;
}

int main(void) {
  int result = check_prefix_postfix_and_wrap();
  if (result != 0)
    return result;
  result = check_assignment_expression_values();
  if (result != 0)
    return 10 + result;
  result = check_single_evaluation();
  if (result != 0)
    return 20 + result;
  result = check_volatile_update();
  if (result != 0)
    return 30 + result;
  result = check_signed_and_boolean_values();
  if (result != 0)
    return 40 + result;
  return 0;
}

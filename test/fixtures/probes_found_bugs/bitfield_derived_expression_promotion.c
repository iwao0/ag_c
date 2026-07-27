// Assignment, compound assignment, prefix update, and comma expressions can
// retain a source bit-field for later integer promotion even though their
// apparent type is the declared field type. Postfix update is the boundary:
// its result no longer receives width-aware bit-field promotion.
#include <limits.h>
#include <stdarg.h>

#define TYPE_IS(expression, type) \
  _Generic((expression), type: 1, default: 0)

struct expression_bits {
  unsigned int narrow : 3;
  unsigned int full : sizeof(unsigned int) * CHAR_BIT;
  unsigned long extended : 3;
};

_Static_assert(
    TYPE_IS(((struct expression_bits){0}).narrow, unsigned int),
    "direct bit-field has its declared type");
_Static_assert(
    TYPE_IS(+((struct expression_bits){0}).narrow, int),
    "direct bit-field undergoes width-aware integer promotion");

static int read_promoted_pair(int marker, ...) {
  va_list arguments;
  va_start(arguments, marker);
  int first = va_arg(arguments, int);
  int second = va_arg(arguments, int);
  va_end(arguments);
  return first * 100 + second;
}

static int check_expression_categories(void) {
  struct expression_bits bits = {0};
  _Static_assert(TYPE_IS((bits.narrow = 1), unsigned int),
                 "assignment result keeps the declared type");
  _Static_assert(TYPE_IS((bits.narrow += 1), unsigned int),
                 "compound assignment result keeps the declared type");
  _Static_assert(TYPE_IS(((void)0, bits.narrow), unsigned int),
                 "comma result keeps the right operand type");
  _Static_assert(TYPE_IS(+(bits.narrow = 1), int),
                 "assignment result retains bit-field promotion behavior");
  _Static_assert(TYPE_IS(+(bits.narrow += 1), int),
                 "compound result retains bit-field promotion behavior");
  _Static_assert(TYPE_IS(+((void)0, bits.narrow), int),
                 "comma result retains bit-field promotion behavior");
  _Static_assert(TYPE_IS(+(++bits.narrow), int),
                 "prefix update retains bit-field promotion behavior");
  _Static_assert(
      TYPE_IS(+(bits.narrow++), unsigned int),
      "postfix update yields its type without bit-field promotion");
  _Static_assert(TYPE_IS(+((void)0, bits.narrow = 1), int),
                 "comma assignment result retains bit-field promotion");
  _Static_assert(TYPE_IS(+(bits.full = 1), unsigned int),
                 "full-width assignment cannot promote to int");
  _Static_assert(
      TYPE_IS(1 ? (bits.narrow = 1) : (bits.narrow = 2), int),
      "conditional operands retain assignment bit-field promotion");
  _Static_assert(
      TYPE_IS(+_Generic(0, default: (bits.narrow = 1)), int),
      "generic selection retains assignment bit-field promotion");
  _Static_assert(TYPE_IS((bits.extended = 1), unsigned long),
                 "extended assignment keeps the declared type");
  _Static_assert(TYPE_IS(+(bits.extended = 1), int),
                 "extended assignment receives width-aware promotion");
  _Static_assert(TYPE_IS(+(++bits.extended), int),
                 "extended prefix update retains bit-field promotion");
  _Static_assert(TYPE_IS(+(bits.extended++), unsigned long),
                 "extended postfix update loses bit-field promotion");
  _Static_assert(
      TYPE_IS(1 ? (bits.extended = 1) : (bits.extended = 2), int),
      "conditional operands promote extended assignment bit-fields");

  unsigned int source = 9;
  int assignment_sum = (bits.narrow = source) + -2;
  if (assignment_sum != -1 || (int)bits.narrow != 1)
    return 1;

  bits.narrow = 0;
  int compound_sum = (bits.narrow += source) + -2;
  if (compound_sum != -1 || (int)bits.narrow != 1)
    return 2;

  bits.narrow = 7;
  int comma_sum = ((void)0, bits.narrow) + -8;
  if (comma_sum != -1 || (int)bits.narrow != 7)
    return 3;

  bits.narrow = 7;
  int prefix_sum = ++bits.narrow + -2;
  if (prefix_sum != -2 || (int)bits.narrow != 0)
    return 4;

  bits.narrow = 7;
  unsigned int postfix_sum = bits.narrow++ + -8;
  if (postfix_sum != ~0u || (int)bits.narrow != 0)
    return 5;

  if (read_promoted_pair(0, (bits.extended = 7ul), 23) != 723 ||
      (int)bits.extended != 7)
    return 6;
  return 0;
}

int main(void) {
  return check_expression_categories();
}

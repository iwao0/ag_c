#include <assert.h>
#include <stdarg.h>

enum code { CODE_VALUE = 42 };

static int check_promotions(int marker, ...) {
  va_list arguments;
  va_start(arguments, marker);
  double floating = va_arg(arguments, double);
  int signed_character = va_arg(arguments, int);
  int unsigned_character = va_arg(arguments, int);
  int short_value = va_arg(arguments, int);
  int unsigned_short_value = va_arg(arguments, int);
  int boolean_value = va_arg(arguments, int);
  int enumeration = va_arg(arguments, int);
  long double extended_floating = va_arg(arguments, long double);
  const int *pointer = va_arg(arguments, const int *);
  va_end(arguments);
  return marker == 7 && floating == 1.5 && signed_character == -2 &&
         unsigned_character == 250 && short_value == -300 &&
         unsigned_short_value == 60000 && boolean_value == 1 &&
         enumeration == 42 && extended_floating == 2.25L &&
         pointer && *pointer == 99;
}

int main(void) {
  float floating = 1.5f;
  signed char signed_character = -2;
  unsigned char unsigned_character = 250;
  short short_value = -300;
  unsigned short unsigned_short_value = 60000;
  _Bool boolean_value = 1;
  enum code enumeration = CODE_VALUE;
  long double extended_floating = 2.25L;
  int pointed_value = 99;
  assert(check_promotions(
      7, floating, signed_character, unsigned_character,
      short_value, unsigned_short_value, boolean_value, enumeration,
      extended_floating, &pointed_value));
  return 0;
}

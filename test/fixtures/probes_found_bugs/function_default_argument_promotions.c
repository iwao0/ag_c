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
  int enumeration = va_arg(arguments, int);
  va_end(arguments);
  return marker == 7 && floating == 1.5 && signed_character == -2 &&
         unsigned_character == 250 && short_value == -300 &&
         enumeration == 42;
}

int main(void) {
  float floating = 1.5f;
  signed char signed_character = -2;
  unsigned char unsigned_character = 250;
  short short_value = -300;
  enum code enumeration = CODE_VALUE;
  assert(check_promotions(
      7, floating, signed_character, unsigned_character,
      short_value, enumeration));
  return 0;
}

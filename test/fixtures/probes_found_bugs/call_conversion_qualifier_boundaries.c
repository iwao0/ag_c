#include <assert.h>
#include <stdarg.h>
#include <stdatomic.h>

static int check_variadic_promotions(int marker, ...) {
  va_list arguments;
  va_start(arguments, marker);
  int signed_character = va_arg(arguments, int);
  int unsigned_character = va_arg(arguments, int);
  int signed_short = va_arg(arguments, int);
  int unsigned_short = va_arg(arguments, int);
  int plain_integer = va_arg(arguments, int);
  va_end(arguments);
  return marker == 9 &&
         signed_character == -12 &&
         unsigned_character == 240 &&
         signed_short == -1234 &&
         unsigned_short == 60000 &&
         plain_integer == 77;
}

static int check_fixed_arguments(int signed_value, unsigned int unsigned_value,
                                 double floating_value) {
  return signed_value == -12 &&
         unsigned_value == 60000u &&
         floating_value == 1.5;
}

static int restrict_accumulate(int *restrict destination,
                               const int *restrict source, int count) {
  for (int i = 0; i < count; ++i) {
    destination[i] += source[i];
  }
  return destination[0] + destination[1] + destination[2];
}

static int integer_from_double(double value) {
  return value;
}

static double double_from_integer(int value) {
  return value;
}

static unsigned char unsigned_char_from_int(int value) {
  return value;
}

static signed char signed_char_from_int(int value) {
  return value;
}

static unsigned short unsigned_short_from_int(int value) {
  return value;
}

static int check_calls_and_returns(void) {
  signed char signed_character = -12;
  unsigned char unsigned_character = 240;
  short signed_short = -1234;
  unsigned short unsigned_short = 60000;
  float floating = 1.5f;

  if (!check_variadic_promotions(
          9, signed_character, unsigned_character, signed_short,
          unsigned_short, 77))
    return 11;
  if (!check_fixed_arguments(
          signed_character, unsigned_short, floating))
    return 12;

  if (integer_from_double(3.75) != 3) return 13;
  if (integer_from_double(-3.75) != -3) return 14;
  if (double_from_integer(-17) != -17.0) return 15;

  int wide_unsigned_char = 511;
  int signed_char_value = -120;
  int wide_unsigned_short = 70000;
  if (unsigned_char_from_int(wide_unsigned_char) != 255) return 16;
  if (signed_char_from_int(signed_char_value) != -120) return 17;
  if (unsigned_short_from_int(wide_unsigned_short) != 4464) return 18;
  return 0;
}

static int check_qualifiers_and_atomic_values(void) {
  volatile int volatile_value = 3;
  volatile int *volatile_pointer = &volatile_value;
  *volatile_pointer += 4;
  assert(volatile_value == 7);
  ++*volatile_pointer;
  assert(*volatile_pointer == 8);

  int destination[3] = {1, 2, 3};
  const int source[3] = {10, 20, 30};
  assert(restrict_accumulate(destination, source, 3) == 66);
  assert(destination[0] == 11);
  assert(destination[1] == 22);
  assert(destination[2] == 33);

  _Atomic int atomic_integer = 5;
  _Atomic long atomic_long = 100;
  atomic_integer += 7;
  ++atomic_integer;
  atomic_long -= 9;
  assert(atomic_integer == 13);
  assert(atomic_long == 91);
  return 0;
}

int main(void) {
  int call_status = check_calls_and_returns();
  if (call_status != 0) return call_status;
  int qualifier_status = check_qualifiers_and_atomic_values();
  if (qualifier_status != 0) return qualifier_status;
  return 0;
}

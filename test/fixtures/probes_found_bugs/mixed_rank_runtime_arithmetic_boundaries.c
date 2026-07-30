/*
 * Exercise the usual arithmetic conversions when rank and signedness select
 * different common types even though the physical IR width is unchanged.
 */
#include <limits.h>

#define TYPE_IS(expression, type) \
  _Generic((expression), type: 1, default: 0)

_Static_assert(
    TYPE_IS((long)1 + (unsigned int)1, long),
    "long represents every unsigned int value");
_Static_assert(
    TYPE_IS((unsigned int)1 + (long)1, long),
    "operand order must not change the common type");
_Static_assert(
    TYPE_IS((long long)1 + (unsigned long)1, unsigned long long),
    "same-width unsigned long requires the unsigned higher-rank type");
_Static_assert(
    TYPE_IS((unsigned long)1 + (long long)1, unsigned long long),
    "same-width rank conversion must be symmetric");
_Static_assert(
    TYPE_IS((int)1 + (unsigned long)1, unsigned long),
    "a higher-rank unsigned type remains unsigned");

static int signed_calls;
static int unsigned_calls;

static long long next_signed_wide(void) {
  signed_calls++;
  return -7LL;
}

static unsigned long next_unsigned_long(void) {
  unsigned_calls++;
  return 3UL;
}

int main(void) {
  long signed_long = -7L;
  unsigned int unsigned_int = 3U;

  if (signed_long + unsigned_int != -4L)
    return 1;
  if (unsigned_int + signed_long != -4L)
    return 2;
  if (signed_long - unsigned_int != -10L)
    return 3;
  if (unsigned_int - signed_long != 10L)
    return 4;
  if (signed_long * unsigned_int != -21L)
    return 5;
  if (signed_long / unsigned_int != -2L)
    return 6;
  if (unsigned_int / signed_long != 0L)
    return 7;
  if (signed_long % unsigned_int != -1L)
    return 8;
  if (unsigned_int % signed_long != 3L)
    return 9;
  if ((signed_long & unsigned_int) != 1L)
    return 10;
  if ((signed_long | unsigned_int) != -5L)
    return 11;
  if ((signed_long ^ unsigned_int) != -6L)
    return 12;
  if (!(signed_long < unsigned_int) || !(unsigned_int > signed_long))
    return 13;

  {
    long long signed_wide = -7LL;
    unsigned long unsigned_long = 3UL;

    if (signed_wide + unsigned_long != 18446744073709551612ULL)
      return 14;
    if (unsigned_long + signed_wide != 18446744073709551612ULL)
      return 15;
    if (signed_wide - unsigned_long != 18446744073709551606ULL)
      return 16;
    if (signed_wide * unsigned_long != 18446744073709551595ULL)
      return 17;
    if (signed_wide / unsigned_long != 6148914691236517203ULL)
      return 18;
    if (signed_wide % unsigned_long != 0ULL)
      return 19;
    if (unsigned_long / signed_wide != 0ULL)
      return 20;
    if (unsigned_long % signed_wide != 3ULL)
      return 21;
    if ((signed_wide & unsigned_long) != 1ULL)
      return 22;
    if ((signed_wide | unsigned_long) != 18446744073709551611ULL)
      return 23;
    if ((signed_wide ^ unsigned_long) != 18446744073709551610ULL)
      return 24;
    if (!(signed_wide > unsigned_long) || !(unsigned_long < signed_wide))
      return 25;
  }

  {
    long add_to_signed = -7L;
    unsigned int add_to_unsigned = 3U;
    long divide_signed = -7L;
    unsigned long add_to_unsigned_long = 3UL;
    unsigned long divide_unsigned_long = 7UL;

    add_to_signed += unsigned_int;
    add_to_unsigned += signed_long;
    divide_signed /= unsigned_int;
    add_to_unsigned_long += -7LL;
    divide_unsigned_long /= -3LL;

    if (add_to_signed != -4L)
      return 26;
    if (add_to_unsigned != UINT_MAX - 3U)
      return 27;
    if (divide_signed != -2L)
      return 28;
    if (add_to_unsigned_long != ULONG_MAX - 3UL)
      return 29;
    if (divide_unsigned_long != 0UL)
      return 30;
  }

  signed_calls = 0;
  unsigned_calls = 0;
  if (next_signed_wide() + next_unsigned_long() !=
      18446744073709551612ULL)
    return 31;
  if (signed_calls != 1 || unsigned_calls != 1)
    return 32;

  return 0;
}

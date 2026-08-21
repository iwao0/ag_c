#include <assert.h>

typedef double long OrderedLongDouble;
typedef signed long long SignedWide;
typedef unsigned long long UnsignedWide;
typedef char unsigned ReorderedUnsignedChar;
typedef short signed ReorderedSignedShort;
typedef int unsigned ReorderedUnsignedInt;
typedef long signed ReorderedSignedLong;
typedef long long unsigned ReorderedUnsignedWide;

static int unsigned reordered_file_object = 17U;

struct Values {
  long const double floating;
  long long signed_integer;
  unsigned long long unsigned_integer;
  long const unsigned reordered_unsigned_long;
};

static double long add(long double left, double long right) {
  return left + right;
}

static long unsigned add_reordered(int signed left, short unsigned right) {
  return (long unsigned)left + (long unsigned)right;
}

int main(void) {
  const long double first = 1.25L;
  long const double second = 2.75L;
  OrderedLongDouble sum = add(first, second);
  struct Values values = {sum, -7LL, 9ULL, 5UL};
  SignedWide signed_wide = values.signed_integer;
  UnsignedWide unsigned_wide = values.unsigned_integer;
  ReorderedUnsignedChar unsigned_char = 250;
  ReorderedSignedShort signed_short = -3;
  ReorderedUnsignedInt unsigned_int = 4U;
  ReorderedSignedLong signed_long = -6L;
  ReorderedUnsignedWide reordered_wide = 11ULL;
  _Atomic(long unsigned) atomic_reordered = 13UL;

  assert(sizeof(OrderedLongDouble) == sizeof(long double));
  assert(sizeof(SignedWide) == sizeof(long long));
  assert(sizeof(UnsignedWide) == sizeof(unsigned long long));
  assert(sizeof(ReorderedUnsignedChar) == sizeof(unsigned char));
  assert(sizeof(ReorderedSignedShort) == sizeof(signed short));
  assert(sizeof(ReorderedUnsignedInt) == sizeof(unsigned int));
  assert(sizeof(ReorderedSignedLong) == sizeof(signed long));
  assert(sizeof(ReorderedUnsignedWide) == sizeof(unsigned long long));
  assert(values.floating == 4.0L);
  assert(signed_wide == -7);
  assert(unsigned_wide == 9);
  assert(values.reordered_unsigned_long == 5UL);
  assert(unsigned_char == 250);
  assert(signed_short == -3);
  assert(unsigned_int == 4U);
  assert(signed_long == -6L);
  assert(reordered_wide == 11ULL);
  assert(atomic_reordered == 13UL);
  assert(add_reordered(2, 3) == 5UL);
  assert(reordered_file_object == 17U);
  return 0;
}

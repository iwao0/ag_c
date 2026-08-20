#include <assert.h>

typedef double long OrderedLongDouble;
typedef signed long long SignedWide;
typedef unsigned long long UnsignedWide;

struct Values {
  long const double floating;
  long long signed_integer;
  unsigned long long unsigned_integer;
};

static double long add(long double left, double long right) {
  return left + right;
}

int main(void) {
  const long double first = 1.25L;
  long const double second = 2.75L;
  OrderedLongDouble sum = add(first, second);
  struct Values values = {sum, -7LL, 9ULL};
  SignedWide signed_wide = values.signed_integer;
  UnsignedWide unsigned_wide = values.unsigned_integer;

  assert(sizeof(OrderedLongDouble) == sizeof(long double));
  assert(sizeof(SignedWide) == sizeof(long long));
  assert(sizeof(UnsignedWide) == sizeof(unsigned long long));
  assert(values.floating == 4.0L);
  assert(signed_wide == -7);
  assert(unsigned_wide == 9);
  return 0;
}

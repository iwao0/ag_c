#include <assert.h>

int main(void) {
  unsigned int u = 5U;
  short s = -2;
  unsigned long long ull = 9ULL;

  assert((short)(s / u) == 13106);
  assert((signed char)(ull <= (long)-6L) == 1);
  assert((char)(ull <= (long)-6L) == 1);

  assert(sizeof((short)(s / u)) == sizeof(short));
  assert(_Generic((short)(s / u), short: 1, int: 2, default: 3) == 1);
  assert(_Generic((char)(ull <= (long)-6L), char: 1, signed char: 2, default: 3) == 1);

  return 0;
}

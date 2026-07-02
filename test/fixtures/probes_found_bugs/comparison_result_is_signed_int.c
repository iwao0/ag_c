#include <assert.h>

#define TYPE_ID(x) _Generic((x), \
  int: 1, \
  unsigned int: 2, \
  long: 3, \
  unsigned long: 4, \
  default: 99)

int main(void) {
  assert(TYPE_ID(((unsigned short)3) < ((unsigned int)5U)) == 1);
  assert(TYPE_ID(((unsigned long)3) == ((int)3)) == 1);
  assert(TYPE_ID(1 && (unsigned int)2) == 1);
  assert(TYPE_ID(0 || (unsigned long)2) == 1);
  assert(TYPE_ID((unsigned int)(((char)1) >= ((unsigned int)5U))) == 2);
  assert(TYPE_ID((int)(((unsigned int)1) + ((unsigned int)2))) == 1);
  assert(_Generic((char)(((unsigned long long)9ULL) <= ((long)-6L)),
                  char: 1, signed char: 2, default: 3) == 1);

  assert(-(((unsigned short)3) < ((unsigned int)5U)) == -1);
  assert((long long)-(((unsigned short)3) < ((unsigned int)5U)) == -1LL);
  assert((unsigned long long)-(((unsigned short)3) < ((unsigned int)5U)) ==
         18446744073709551615ULL);

  return 0;
}

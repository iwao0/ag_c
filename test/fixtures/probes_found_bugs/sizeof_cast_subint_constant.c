#include <assert.h>

int main(void) {
  assert(sizeof((char)1) == 1);
  assert(sizeof((signed char)-1) == 1);
  assert(sizeof((unsigned char)1) == 1);
  assert(sizeof((short)-2) == 2);
  assert(sizeof((unsigned short)1) == 2);

  assert(_Generic((char)1, char: 1, int: 2, default: 3) == 1);
  assert(_Generic((signed char)-1, signed char: 1, int: 2, default: 3) == 1);
  assert(_Generic((unsigned char)1, unsigned char: 1, int: 2, default: 3) == 1);
  assert(_Generic((short)-2, short: 1, int: 2, default: 3) == 1);
  assert(_Generic((unsigned short)1, unsigned short: 1, int: 2, default: 3) == 1);

  int i = -1;
  assert(sizeof((signed char)i) == 1);
  assert(sizeof((unsigned char)i) == 1);
  assert(sizeof((short)i) == 2);
  assert(sizeof((unsigned short)i) == 2);

  assert((long long)((signed char)-81 | (unsigned char)22) == -65);
  assert((unsigned long long)((signed char)-81 | (unsigned char)22) == 18446744073709551551ULL);

  return 0;
}

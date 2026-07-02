#include <assert.h>

#define TYPE_ID(x) _Generic((x), \
  long: 1, \
  unsigned long: 2, \
  long long: 3, \
  unsigned long long: 4, \
  default: 99)

int main(void) {
  assert(TYPE_ID(((unsigned long long)9ULL) ^ ((unsigned short)3)) == 4);
  assert(TYPE_ID(((long long)-9LL) + ((long)3L)) == 3);
  assert(TYPE_ID(((long long)-9LL) + ((unsigned long)3UL)) == 4);
  assert(TYPE_ID(((unsigned long long)9ULL) >> 1) == 4);
  assert(TYPE_ID(((long long)9LL) << 1) == 3);
  assert(_Generic((unsigned int)(((unsigned long long)9ULL) + ((unsigned long long)9ULL)),
                  unsigned int: 1, unsigned long long: 2, default: 3) == 1);
  assert(sizeof((unsigned int)(((unsigned long long)9ULL) + ((unsigned long long)9ULL))) ==
         sizeof(unsigned int));
  assert(_Generic((1 ? (((short)-2) == ((short)-2)) : ((unsigned long long)9ULL)),
                  unsigned long long: 1, unsigned long: 2, default: 3) == 1);

  assert((unsigned long long)(((unsigned long long)9ULL) ^ ((unsigned short)3)) == 10ULL);
  assert((long long)(((long long)-9LL) + ((long)3L)) == -6LL);

  return 0;
}

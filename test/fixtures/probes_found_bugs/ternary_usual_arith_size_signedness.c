#include <assert.h>

int main(void) {
  assert(sizeof(0 ? (unsigned char)100 : (long)-1L) == sizeof(long));
  assert((long long)(0 ? (unsigned char)100 : (long)-1L) == -1LL);
  assert((unsigned long long)(0 ? (unsigned char)100 : (long)-1L) == 18446744073709551615ULL);

  unsigned char uc = 100;
  long ln = -1L;
  assert(sizeof(1 ? uc : ln) == sizeof(long));
  assert((long long)(1 ? uc : ln) == 100LL);

  unsigned int ui = 4000000000U;
  assert(sizeof(1 ? ui : -1L) == sizeof(long));
  assert((long long)(1 ? ui : -1L) == 4000000000LL);
  assert((unsigned long long)(1 ? ui : -1L) == 4000000000ULL);

  unsigned long ul = 9UL;
  assert(sizeof(1 ? ui : ul) == sizeof(unsigned long));
  assert((unsigned long long)(1 ? ui : ul) == 4000000000ULL);
  assert(_Generic((0 ? (unsigned long)4000000000UL : (long)-1L),
                  unsigned long: 1, long: 2, default: 3) == 1);

  return 0;
}

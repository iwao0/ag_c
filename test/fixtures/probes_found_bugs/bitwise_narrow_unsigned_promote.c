#include <assert.h>
#include <stddef.h>

int main(void) {
  signed char sc = -81;
  unsigned char uc = 22;

  int r = sc | uc;
  assert(r == -65);
  assert(sizeof(sc | uc) == sizeof(int));

  long long s = (long long)(sc | uc);
  unsigned long long u = (unsigned long long)(sc | uc);
  assert(s == -65);
  assert(u == 18446744073709551551ULL);
  assert((long long)((signed char)-81 | (unsigned char)22) == -65);
  assert((unsigned long long)((signed char)-81 | (unsigned char)22) == 18446744073709551551ULL);

  unsigned short us = 65000;
  short ss = -1;
  assert((us & ss) == 65000);
  assert(sizeof(us & ss) == sizeof(int));
  assert((long long)(us & ss) == 65000);

  unsigned int ui = 4000000000U;
  long sl = -1;
  long mix = sl & ui;
  assert(mix == 4000000000L);
  assert((long long)(sl & ui) == 4000000000LL);

  return 0;
}

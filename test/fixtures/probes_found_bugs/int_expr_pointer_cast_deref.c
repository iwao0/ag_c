// Integer-expression to pointer casts must produce a pointer-typed cast wrapper.
// Otherwise `*(int *)addr` keeps seeing the original scalar `addr` node and loses
// the cast result's pointee size/signedness metadata.
#include <assert.h>

int main(void) {
  int x = 0x12345678;
  long addr = (long)&x;

  assert(*(int *)addr == 0x12345678);

  unsigned char *uc = (unsigned char *)addr;
  assert(uc[0] == 0x78);

  return 0;
}

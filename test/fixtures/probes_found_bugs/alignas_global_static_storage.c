#include <assert.h>

_Alignas(64) int aligned_global = 41;
static _Alignas(128) char aligned_static = 1;
_Alignas(0) int zero_alignment = 1;

static int check_static_local(void) {
  static _Alignas(64) int aligned_local = 42;
  assert((long)&aligned_local % 64 == 0);
  return aligned_local;
}

int main(void) {
  assert((long)&aligned_global % 64 == 0);
  assert((long)&aligned_static % 128 == 0);
  assert(aligned_global + aligned_static == 42);
  assert(zero_alignment == 1);
  assert(check_static_local() == 42);
  return 0;
}

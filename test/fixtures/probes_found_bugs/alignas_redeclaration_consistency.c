#include <assert.h>

extern int plain_extern_then_aligned;
_Alignas(64) int plain_extern_then_aligned = 11;

_Alignas(32) extern int aligned_with_plain_extern;
extern int aligned_with_plain_extern;
_Alignas(32) int aligned_with_plain_extern = 31;

int tentative_then_aligned;
_Alignas(16) int tentative_then_aligned;

_Alignas(0) extern int repeated_zero_alignment;
_Alignas(0) int repeated_zero_alignment = 1;

_Alignas(128) int aligned_definition = 42;
extern int aligned_definition;

static int read_through_block_extern(void) {
  extern int aligned_with_plain_extern;
  return aligned_with_plain_extern;
}

int main(void) {
  assert((long)&plain_extern_then_aligned % 64 == 0);
  assert((long)&aligned_with_plain_extern % 32 == 0);
  assert((long)&tentative_then_aligned % 16 == 0);
  assert((long)&aligned_definition % 128 == 0);
  assert(plain_extern_then_aligned +
             read_through_block_extern() == 42);
  assert(repeated_zero_alignment == 1);
  assert(aligned_definition == 42);
  return 0;
}

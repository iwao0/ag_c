/*
 * Assignment from a narrow extended bit-field is promoted to int as the
 * switch control type. UINT_MAX and -1 therefore denote the same case value.
 * Expect ag_c E3060.
 */
#include <limits.h>

struct switch_bits {
  unsigned long narrow : 3;
};

int main(void) {
  struct switch_bits bits = {0};
  switch (bits.narrow = 1ul) {
    case -1:
      return 1;
    case UINT_MAX:
      return 2;
    default:
      return 0;
  }
}

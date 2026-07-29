/*
 * Consecutive compound statements have disjoint VLA lifetimes. Each block
 * must release its dynamic stack storage before the next block begins.
 */
#include <stddef.h>

#ifdef __wasm32__
#define SCOPE_BYTES (24 * 1024)
#else
#define SCOPE_BYTES (3 * 1024 * 1024)
#endif

#define CHECK_SCOPE(SEED)                                                    \
  {                                                                          \
    int extent = SCOPE_BYTES + (SEED);                                       \
    volatile unsigned char bytes[extent];                                    \
    bytes[0] = (unsigned char)(SEED);                                        \
    bytes[extent - 1] = (unsigned char)((SEED) + 1);                         \
    checksum += bytes[0] + bytes[extent - 1] +                               \
                (unsigned)(sizeof(bytes) == (size_t)extent) - 1;             \
  }

int main(void) {
  unsigned checksum = 0;
  CHECK_SCOPE(0)
  CHECK_SCOPE(1)
  CHECK_SCOPE(2)
  CHECK_SCOPE(3)
  CHECK_SCOPE(4)
  CHECK_SCOPE(5)
  CHECK_SCOPE(6)
  CHECK_SCOPE(7)
  CHECK_SCOPE(8)
  CHECK_SCOPE(9)
  CHECK_SCOPE(10)
  CHECK_SCOPE(11)
  CHECK_SCOPE(12)
  CHECK_SCOPE(13)
  CHECK_SCOPE(14)
  CHECK_SCOPE(15)
  CHECK_SCOPE(16)
  CHECK_SCOPE(17)
  CHECK_SCOPE(18)
  CHECK_SCOPE(19)
  CHECK_SCOPE(20)
  CHECK_SCOPE(21)
  CHECK_SCOPE(22)
  CHECK_SCOPE(23)
  return checksum == 576 ? 0 : 1;
}

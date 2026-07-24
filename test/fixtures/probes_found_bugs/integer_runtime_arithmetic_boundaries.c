#include <assert.h>
#include <limits.h>

struct compound_values {
  unsigned char byte;
  unsigned short half;
  unsigned long bits;
};

static int check_compound_assignment_conversions(void) {
  unsigned char wrapped = 250;
  wrapped += 10;
  assert(wrapped == 4);

  unsigned short products[2] = {30000, 0xfff0};
  products[0] *= 3;
  assert(products[0] == 24464);
  products[1] += 0x20;
  assert(products[1] == 0x10);

  struct compound_values value = {0x81, 0xf0f0, 0xff00ul};
  value.byte >>= 1;
  assert(value.byte == 0x40);
  value.half &= 0x0ff0;
  assert(value.half == 0x00f0);
  value.bits |= 0x55ul;
  assert(value.bits == 0xff55ul);
  value.bits ^= 0x0f0ful;
  assert(value.bits == 0xf05aul);
  value.bits %= 257ul;
  assert(value.bits == 107ul);

  unsigned char pointee = 65;
  unsigned char *pointer = &pointee;
  *pointer <<= 2;
  assert(pointee == 4);

  long quotient = -29;
  long remainder = -29;
  quotient /= 4;
  remainder %= 4;
  assert(quotient == -7);
  assert(remainder == -1);
  return 0;
}

static int check_shift_boundaries(void) {
  unsigned int high_bit = 1u << 31;
  assert(high_bit == 0x80000000u);
  assert((high_bit >> 31) == 1u);
  assert((~0u >> 1) == 0x7fffffffu);

  unsigned long long_high_bit = 1ul << 63;
  assert(long_high_bit == 0x8000000000000000ul);
  assert((long_high_bit >> 63) == 1ul);
  assert((~0ul >> 1) == 0x7ffffffffffffffful);

  signed char signed_byte = -2;
  unsigned char unsigned_byte = 0x80;
  assert((signed_byte >> 1) == -1);
  assert((unsigned_byte >> 7) == 1);

  long signed_long = -8;
  assert((signed_long >> 2) == -2);

  unsigned int count = 0;
  assert((0x12345678u << count) == 0x12345678u);
  count = 31;
  assert((1u << count) == high_bit);
  return 0;
}

static int check_signed_division_and_remainder(void) {
  assert(-7 / 3 == -2);
  assert(-7 % 3 == -1);
  assert(7 / -3 == -2);
  assert(7 % -3 == 1);
  assert(-7 / -3 == 2);
  assert(-7 % -3 == -1);

  long negative_long = -9223372036854775807l;
  assert(negative_long / 3 == -3074457345618258602l);
  assert(negative_long % 3 == -1);
  assert((-9223372036854775807l - 1l) / 2 ==
         -4611686018427387904l);

  long long negative_long_long = -9000000000000000001ll;
  assert(negative_long_long / 7ll == -1285714285714285714ll);
  assert(negative_long_long % 7ll == -3ll);

  assert(LONG_MAX / 7 > 0);
  return 0;
}

int main(void) {
  assert(check_compound_assignment_conversions() == 0);
  assert(check_shift_boundaries() == 0);
  assert(check_signed_division_and_remainder() == 0);
  return 0;
}

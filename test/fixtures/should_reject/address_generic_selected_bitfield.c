/*
 * A generic selection has the value category of its selected association.
 * Selecting a bit-field therefore does not make it addressable.
 * Expect ag_c E3113.
 */
struct address_bits {
  unsigned int narrow : 3;
};

int main(void) {
  struct address_bits bits = {0};
  return &_Generic(
             0, int: bits.narrow, default: bits) != 0;
}

/*
 * A generic selection inherits the selected expression's bit-field
 * designation, so sizeof cannot be applied to it. Expect ag_c E3118.
 */
struct generic_bits {
  unsigned int narrow : 3;
};

int main(void) {
  struct generic_bits bits = {0};
  return (int)sizeof(
      _Generic(0, int: bits.narrow, default: 0u));
}

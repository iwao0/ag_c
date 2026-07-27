// Integer promotion can retain a source bit-field through these expressions,
// but the expressions themselves do not designate a bit-field for sizeof.
// sizeof is unevaluated, so none of the apparent updates may take effect.
struct sizeof_bits {
  unsigned int narrow : 3;
};

static int check_derived_sizeof(void) {
  struct sizeof_bits bits = {0};
  _Static_assert(sizeof(bits.narrow = 1) == sizeof(unsigned int),
                 "assignment result is not a bit-field designator");
  _Static_assert(sizeof(bits.narrow += 1) == sizeof(unsigned int),
                 "compound result is not a bit-field designator");
  _Static_assert(sizeof(++bits.narrow) == sizeof(unsigned int),
                 "prefix result is not a bit-field designator");
  _Static_assert(sizeof(bits.narrow++) == sizeof(unsigned int),
                 "postfix result is not a bit-field designator");
  _Static_assert(sizeof(((void)0, bits.narrow)) == sizeof(unsigned int),
                 "comma result is not a bit-field designator");
  _Static_assert(
      sizeof(_Generic(0, int: 1u, default: bits.narrow)) ==
          sizeof(unsigned int),
      "unselected bit-field association does not constrain sizeof");

  if (bits.narrow != 0)
    return 1;
  return 0;
}

int main(void) {
  return check_derived_sizeof();
}

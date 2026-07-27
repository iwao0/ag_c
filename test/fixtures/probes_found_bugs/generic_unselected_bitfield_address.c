// A generic selection has the value category of the selected association.
// An unselected bit-field must not make the selected ordinary lvalue
// unaddressable.
struct address_bits {
  unsigned int narrow : 3;
};

int main(void) {
  struct address_bits bits = {0};
  int value = 3;
  int *pointer = &_Generic(
      0, int: value, default: bits.narrow);
  *pointer = 7;
  return value == 7 && bits.narrow == 0 ? 0 : 1;
}

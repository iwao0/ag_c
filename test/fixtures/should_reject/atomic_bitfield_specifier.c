/* An atomic type specifier cannot form a bit-field type. */
struct flags {
  _Atomic(unsigned int) value : 3;
};

int main(void) {
  return 0;
}

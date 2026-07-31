/* An alignment specifier cannot be applied to a bit-field. */
struct record {
  _Alignas(8) unsigned int bit : 1;
};
int main(void) { return 0; }

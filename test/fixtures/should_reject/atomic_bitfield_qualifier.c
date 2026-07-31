/* An atomic-qualified type cannot be used as a bit-field type. */
struct flags {
  _Atomic unsigned int value : 3;
};

int main(void) {
  return 0;
}

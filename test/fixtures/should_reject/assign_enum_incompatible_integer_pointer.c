/* A pointer to an enum object is not assignable to an incompatible integer pointer. */
enum positive_value {
  POSITIVE_ZERO = 0,
  POSITIVE_ONE = 1
};

int main(void) {
  enum positive_value value = POSITIVE_ONE;
  int *pointer = &value;
  return *pointer;
}

/* Nested enum and integer pointers do not gain assignment compatibility. */
enum positive_value {
  POSITIVE_ZERO = 0,
  POSITIVE_ONE = 1
};

int main(void) {
  enum positive_value value = POSITIVE_ONE;
  enum positive_value *pointer = &value;
  int **nested = &pointer;
  return **nested;
}

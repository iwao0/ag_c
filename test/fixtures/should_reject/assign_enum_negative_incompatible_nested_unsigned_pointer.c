/* A signed-compatible enum pointer is not assignable through unsigned pointer levels. */
enum negative_value {
  NEGATIVE_ONE = -1,
  NEGATIVE_ZERO = 0
};

int main(void) {
  enum negative_value value = NEGATIVE_ONE;
  enum negative_value *pointer = &value;
  unsigned int **nested = &pointer;
  return **nested;
}

/* Pointers to distinct enum types are not assignment-compatible. */
enum first_value {
  FIRST_ZERO = 0,
  FIRST_ONE = 1
};

enum second_value {
  SECOND_ZERO = 0,
  SECOND_ONE = 1
};

int main(void) {
  enum second_value value = SECOND_ONE;
  enum first_value *pointer = &value;
  return *pointer;
}

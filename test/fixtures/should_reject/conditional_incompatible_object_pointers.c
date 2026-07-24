int main(void) {
  int integer_value = 0;
  double floating_value = 0.0;
  void *invalid_pointer =
      1 ? &integer_value : &floating_value;
  return invalid_pointer == 0;
}

/* Function argument conversion cannot discard a pointee const qualifier. */
static int read_mutable(int *value) {
  return *value;
}

int main(void) {
  const int value = 7;
  return read_mutable(&value);
}

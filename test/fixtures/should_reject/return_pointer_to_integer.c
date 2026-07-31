/* An object pointer is not implicitly convertible to an integer return type. */
static int get(int *value) {
  return value;
}

int main(void) {
  int value = 1;
  return get(&value);
}

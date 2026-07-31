/* A nonzero integer is not implicitly convertible to a pointer return type. */
static int *get(void) {
  return 5;
}

int main(void) {
  return get() != 0;
}

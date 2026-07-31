/* A return conversion cannot convert between incompatible object pointers. */
static int *get(double *value) {
  return value;
}

int main(void) {
  return get(0) != 0;
}

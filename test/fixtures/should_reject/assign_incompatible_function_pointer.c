/* Function pointers with incompatible parameter and return types cannot convert. */
static int increment(int value) {
  return value + 1;
}

int main(void) {
  double (*invalid)(double) = increment;
  return invalid(1.0) != 2.0;
}

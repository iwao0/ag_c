/* Equality cannot compare pointers to incompatible function types. */
static int increment(int value) {
  return value + 1;
}

static double half(double value) {
  return value / 2.0;
}

int main(void) {
  return increment == half;
}

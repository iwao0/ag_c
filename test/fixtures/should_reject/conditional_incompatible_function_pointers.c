/* A conditional expression cannot combine incompatible function pointers. */
static int first(int value) {
  return value;
}

static int second(double value) {
  return (int)value;
}

int main(void) {
  return (1 ? first : second) != 0;
}

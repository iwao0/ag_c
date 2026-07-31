/* Parameter names in a function definition must be unique. */
static int invalid(int value, int value) {
  return value;
}

int main(void) {
  return invalid(1, 2);
}

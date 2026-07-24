static int increment(int value) {
  return value + 1;
}

static double (*invalid_return(void))(double) {
  return increment;
}

int main(void) {
  return invalid_return()(1.0) != 2.0;
}

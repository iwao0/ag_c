static int increment(int value) {
  return value + 1;
}

static double apply(double (*callback)(double), double value) {
  return callback(value);
}

int main(void) {
  return apply(increment, 1.0) != 2.0;
}

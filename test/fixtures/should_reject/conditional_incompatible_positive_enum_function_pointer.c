enum Positive {
  POSITIVE_ZERO,
  POSITIVE_ONE
};

static enum Positive positive_result(void) {
  return POSITIVE_ONE;
}

static int signed_result(void) {
  return 1;
}

int main(void) {
  int (*function)(void) =
      1 ? positive_result : signed_result;
  return function();
}

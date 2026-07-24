enum Negative {
  NEGATIVE_ONE = -1,
  NEGATIVE_ZERO
};

static enum Negative negative_result(void) {
  return NEGATIVE_ONE;
}

static unsigned int unsigned_result(void) {
  return 1u;
}

int main(void) {
  unsigned int (*function)(void) =
      1 ? negative_result : unsigned_result;
  return (int)function();
}

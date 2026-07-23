typedef void no_parameters;

static int answer(no_parameters);
static int registered_answer(register void);

static int answer(void) {
  return 42;
}

static int registered_answer(void) {
  return 7;
}

int main(void) {
  if (answer() != 42) return 1;
  if (registered_answer() != 7) return 2;
  return 0;
}

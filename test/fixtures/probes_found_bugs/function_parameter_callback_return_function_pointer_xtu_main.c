int invoke_callback_factory(
    int (*callback(void))(int), int value);

static int add_two_from_factory(int value) {
  return value + 2;
}

static int (*make_callback(void))(int) {
  return add_two_from_factory;
}

int main(void) {
  return invoke_callback_factory(make_callback, 40);
}

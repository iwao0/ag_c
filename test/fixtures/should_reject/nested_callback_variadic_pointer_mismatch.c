typedef int fixed_callback_function(int value);
typedef int variadic_callback_function(int value, ...);

static int consume_callback(variadic_callback_function *callback) {
  return callback(42, 0);
}

int main(void) {
  int (*invalid)(fixed_callback_function *) = consume_callback;
  return invalid == 0;
}

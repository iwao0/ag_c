typedef int fixed_callback_function(int value);
typedef int variadic_callback_function(int value, ...);

int consume_callback(fixed_callback_function *callback);

int consume_callback(variadic_callback_function *callback) {
  return callback(42, 0);
}

int main(void) {
  return 0;
}

typedef int unprototyped_callback_function();
typedef int narrow_callback_function(signed char value);

int consume_callback(unprototyped_callback_function *callback);

int consume_callback(narrow_callback_function *callback) {
  return callback(42);
}

int main(void) {
  return 0;
}

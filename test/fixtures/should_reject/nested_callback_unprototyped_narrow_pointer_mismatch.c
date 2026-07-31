typedef int unprototyped_callback_function();
typedef int narrow_callback_function(signed char value);

static int consume_callback(narrow_callback_function *callback) {
  return callback(42);
}

int main(void) {
  int (*invalid)(unprototyped_callback_function *) = consume_callback;
  return invalid == 0;
}

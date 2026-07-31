typedef int plain_callback_function(int *value);
typedef int atomic_callback_function(
    _Atomic(int *) value);
typedef int plain_consumer_function(
    plain_callback_function *callback);

static int consume_callback(
    atomic_callback_function *callback) {
  int pointee = 42;
  _Atomic(int *) value = &pointee;

  return callback(value);
}

int main(void) {
  plain_consumer_function *invalid = consume_callback;
  return invalid == 0;
}

typedef int plain_callback_function(_Bool value);
typedef int atomic_callback_function(
    _Atomic(_Bool) value);
typedef int plain_consumer_function(
    plain_callback_function *callback);

static int consume_callback(
    atomic_callback_function *callback) {
  _Atomic(_Bool) value = 1;
  return callback(value);
}

int main(void) {
  plain_consumer_function *invalid = consume_callback;
  return invalid == 0;
}

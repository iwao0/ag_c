typedef int plain_callback_function(long value);
typedef int atomic_callback_function(
    _Atomic(long) value);
typedef int plain_consumer_function(
    plain_callback_function *callback);

static int consume_callback(
    atomic_callback_function *callback) {
  _Atomic(long) value = -5000000000L;
  return callback(value);
}

int main(void) {
  plain_consumer_function *invalid = consume_callback;
  return invalid == 0;
}

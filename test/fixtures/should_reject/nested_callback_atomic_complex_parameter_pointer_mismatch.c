typedef int plain_callback_function(float _Complex value);
typedef int atomic_callback_function(
    _Atomic(float _Complex) value);
typedef int plain_consumer_function(
    plain_callback_function *callback);

static int consume_callback(
    atomic_callback_function *callback) {
  _Atomic(float _Complex) value = 0;
  return callback(value);
}

int main(void) {
  plain_consumer_function *invalid = consume_callback;
  return invalid == 0;
}

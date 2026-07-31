typedef long plain_callback_function(void);
typedef _Atomic(long) atomic_callback_function(void);
typedef int plain_consumer_function(
    plain_callback_function *callback);

static int consume_callback(
    atomic_callback_function *callback) {
  _Atomic(long) value = callback();
  return value == 0;
}

int main(void) {
  plain_consumer_function *invalid = consume_callback;
  return invalid == 0;
}

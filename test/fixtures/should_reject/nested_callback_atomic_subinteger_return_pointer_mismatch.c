typedef _Bool plain_callback_function(void);
typedef _Atomic(_Bool) atomic_callback_function(void);
typedef int plain_consumer_function(
    plain_callback_function *callback);

static int consume_callback(
    atomic_callback_function *callback) {
  _Atomic(_Bool) value = callback();
  return value;
}

int main(void) {
  plain_consumer_function *invalid = consume_callback;
  return invalid == 0;
}

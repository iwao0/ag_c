typedef _Atomic(long) signed_callback_function(void);
typedef _Atomic(unsigned long)
    unsigned_callback_function(void);
typedef int signed_consumer_function(
    signed_callback_function *callback);

static int consume_callback(
    unsigned_callback_function *callback) {
  _Atomic(unsigned long) value = callback();
  return value == 0;
}

int main(void) {
  signed_consumer_function *invalid = consume_callback;
  return invalid == 0;
}

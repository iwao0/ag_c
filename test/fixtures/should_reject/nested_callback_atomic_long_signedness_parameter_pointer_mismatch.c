typedef int signed_callback_function(
    _Atomic(long) value);
typedef int unsigned_callback_function(
    _Atomic(unsigned long) value);
typedef int signed_consumer_function(
    signed_callback_function *callback);

static int consume_callback(
    unsigned_callback_function *callback) {
  _Atomic(unsigned long) value = 4000000000UL;
  return callback(value);
}

int main(void) {
  signed_consumer_function *invalid = consume_callback;
  return invalid == 0;
}

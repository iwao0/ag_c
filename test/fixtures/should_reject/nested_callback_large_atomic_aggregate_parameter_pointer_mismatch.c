struct words3 {
  unsigned int first;
  unsigned int second;
  unsigned int third;
};

typedef int plain_callback_function(struct words3 value);
typedef int atomic_callback_function(_Atomic(struct words3) value);
typedef int plain_consumer_function(
    plain_callback_function *callback);

static int consume_callback(
    atomic_callback_function *callback) {
  _Atomic(struct words3) value = (struct words3){19, 11, 12};
  return callback(value);
}

int main(void) {
  plain_consumer_function *invalid = consume_callback;
  return invalid == 0;
}

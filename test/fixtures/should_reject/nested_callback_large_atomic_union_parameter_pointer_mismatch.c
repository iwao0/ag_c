union words3 {
  unsigned int words[3];
  unsigned char bytes[12];
};

typedef int plain_callback_function(union words3 value);
typedef int atomic_callback_function(_Atomic(union words3) value);
typedef int plain_consumer_function(
    plain_callback_function *callback);

static int consume_callback(
    atomic_callback_function *callback) {
  _Atomic(union words3) value =
      (union words3){.words = {19, 11, 12}};
  return callback(value);
}

int main(void) {
  plain_consumer_function *invalid = consume_callback;
  return invalid == 0;
}

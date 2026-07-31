struct words3 {
  unsigned int first;
  unsigned int second;
  unsigned int third;
};

typedef unsigned int plain_callback_function(
    struct words3 value);

unsigned int consume_atomic_words(
    plain_callback_function *callback) {
  return callback((struct words3){17, 13, 12});
}

union word1 {
  unsigned int bits;
  float value;
};

typedef unsigned int plain_callback_function(
    union word1 value);

unsigned int consume_atomic_small_union(
    plain_callback_function *callback) {
  return callback((union word1){.bits = 42u});
}

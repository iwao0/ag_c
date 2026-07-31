union words3 {
  unsigned int words[3];
  unsigned char bytes[12];
};

typedef unsigned int plain_callback_function(
    union words3 value);

unsigned int consume_atomic_union(
    plain_callback_function *callback) {
  return callback((union words3){.words = {17, 13, 12}});
}

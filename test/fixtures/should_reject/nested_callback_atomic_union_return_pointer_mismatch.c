union wide {
  unsigned long long words[2];
  unsigned char bytes[16];
};

typedef union wide plain_callback_function(void);
typedef _Atomic(union wide) atomic_callback_function(void);
typedef int plain_consumer_function(
    plain_callback_function *callback);

static int consume_callback(
    atomic_callback_function *callback) {
  _Atomic(union wide) value = callback();
  return sizeof value;
}

int main(void) {
  plain_consumer_function *invalid = consume_callback;
  return invalid == 0;
}

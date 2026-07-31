struct words3 {
  unsigned int first;
  unsigned int second;
  unsigned int third;
};

typedef struct words3 plain_callback_function(void);
typedef _Atomic(struct words3) atomic_callback_function(void);
typedef int plain_consumer_function(
    plain_callback_function *callback);

static int consume_callback(
    atomic_callback_function *callback) {
  _Atomic(struct words3) value = callback();
  return sizeof value;
}

int main(void) {
  plain_consumer_function *invalid = consume_callback;
  return invalid == 0;
}

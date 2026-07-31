struct pair {
  int left;
  int right;
};

typedef struct pair plain_callback_function(void);
typedef _Atomic(struct pair) atomic_callback_function(void);
typedef int plain_consumer_function(plain_callback_function *callback);

static int consume_callback(atomic_callback_function *callback) {
  _Atomic(struct pair) value = callback();
  return sizeof value;
}

int main(void) {
  plain_consumer_function *invalid = consume_callback;
  return invalid == 0;
}

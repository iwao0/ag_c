struct pair {
  int left;
  int right;
};

typedef struct pair plain_callback_function(void);
typedef _Atomic(struct pair) atomic_callback_function(void);

int consume_callback(plain_callback_function *callback);

int consume_callback(atomic_callback_function *callback) {
  _Atomic(struct pair) value = callback();
  return sizeof value;
}

int main(void) {
  return 0;
}

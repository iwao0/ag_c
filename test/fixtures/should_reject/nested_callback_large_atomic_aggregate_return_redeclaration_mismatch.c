struct words3 {
  unsigned int first;
  unsigned int second;
  unsigned int third;
};

typedef struct words3 plain_callback_function(void);
typedef _Atomic(struct words3) atomic_callback_function(void);

int consume_callback(plain_callback_function *callback);

int consume_callback(atomic_callback_function *callback) {
  _Atomic(struct words3) value = callback();
  return sizeof value;
}

int main(void) {
  return 0;
}

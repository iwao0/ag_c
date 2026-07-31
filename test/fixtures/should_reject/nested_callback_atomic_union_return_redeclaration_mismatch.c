union word {
  unsigned int bits;
  float value;
};

typedef union word plain_callback_function(void);
typedef _Atomic(union word) atomic_callback_function(void);

int consume_callback(plain_callback_function *callback);

int consume_callback(atomic_callback_function *callback) {
  _Atomic(union word) value = callback();
  return sizeof value;
}

int main(void) {
  return 0;
}

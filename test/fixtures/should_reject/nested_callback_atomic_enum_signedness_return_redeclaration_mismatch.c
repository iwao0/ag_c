enum unsigned_state {
  UNSIGNED_STATE_ZERO = 0,
  UNSIGNED_STATE_VALUE = 42
};

typedef _Atomic(enum unsigned_state)
    enum_callback_function(void);
typedef _Atomic(int) signed_callback_function(void);

int consume_callback(enum_callback_function *callback);

int consume_callback(signed_callback_function *callback) {
  _Atomic(int) value = callback();
  return value;
}

int main(void) {
  return 0;
}

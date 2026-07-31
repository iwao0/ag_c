enum unsigned_state {
  UNSIGNED_STATE_ZERO = 0,
  UNSIGNED_STATE_VALUE = 42
};

typedef int enum_callback_function(
    _Atomic(enum unsigned_state) value);
typedef int signed_callback_function(
    _Atomic(int) value);

int consume_callback(enum_callback_function *callback);

int consume_callback(signed_callback_function *callback) {
  _Atomic(int) value = 42;
  return callback(value);
}

int main(void) {
  return 0;
}

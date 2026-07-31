typedef _Atomic(long) long_callback_function(void);
typedef _Atomic(long long)
    long_long_callback_function(void);

int consume_callback(long_callback_function *callback);

int consume_callback(long_long_callback_function *callback) {
  _Atomic(long long) value = callback();
  return value == 0;
}

int main(void) {
  return 0;
}

typedef _Atomic(double) double_callback_function(void);
typedef _Atomic(long double)
    long_double_callback_function(void);

int consume_callback(double_callback_function *callback);

int consume_callback(long_double_callback_function *callback) {
  _Atomic(long double) value = callback();
  return value == 0.0L;
}

int main(void) {
  return 0;
}

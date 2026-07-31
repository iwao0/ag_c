typedef int double_callback_function(
    _Atomic(double) value);
typedef int long_double_callback_function(
    _Atomic(long double) value);

int consume_callback(double_callback_function *callback);

int consume_callback(long_double_callback_function *callback) {
  _Atomic(long double) value = 17.5L;
  return callback(value);
}

int main(void) {
  return 0;
}

typedef int long_callback_function(
    _Atomic(long) value);
typedef int long_long_callback_function(
    _Atomic(long long) value);

int consume_callback(long_callback_function *callback);

int consume_callback(long_long_callback_function *callback) {
  _Atomic(long long) value = -5000000000LL;
  return callback(value);
}

int main(void) {
  return 0;
}

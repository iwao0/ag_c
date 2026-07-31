typedef int plain_callback_function(double _Complex value);
typedef int atomic_callback_function(
    _Atomic(double _Complex) value);

int consume_callback(plain_callback_function *callback);

int consume_callback(atomic_callback_function *callback) {
  _Atomic(double _Complex) value = 0;
  return callback(value);
}

int main(void) {
  return 0;
}

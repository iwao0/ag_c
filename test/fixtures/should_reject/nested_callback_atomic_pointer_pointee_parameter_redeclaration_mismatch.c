typedef int atomic_pointer_callback_function(
    _Atomic(int *) value);
typedef int atomic_pointee_callback_function(
    _Atomic(int) *value);

int consume_callback(
    atomic_pointer_callback_function *callback);

int consume_callback(
    atomic_pointee_callback_function *callback) {
  _Atomic(int) value = 42;
  return callback(&value);
}

int main(void) {
  return 0;
}

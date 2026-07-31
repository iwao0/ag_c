typedef int unsigned_callback_function(
    _Atomic(unsigned short) value);
typedef int signed_callback_function(
    _Atomic(short) value);

int consume_callback(unsigned_callback_function *callback);

int consume_callback(signed_callback_function *callback) {
  _Atomic(short) value = -17;
  return callback(value);
}

int main(void) {
  return 0;
}

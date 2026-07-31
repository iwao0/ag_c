typedef _Atomic(unsigned short)
    unsigned_callback_function(void);
typedef _Atomic(short) signed_callback_function(void);

int consume_callback(unsigned_callback_function *callback);

int consume_callback(signed_callback_function *callback) {
  _Atomic(short) value = callback();
  return value;
}

int main(void) {
  return 0;
}

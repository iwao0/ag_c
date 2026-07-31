union words3 {
  unsigned int words[3];
  unsigned char bytes[12];
};

typedef int plain_callback_function(union words3 value);
typedef int atomic_callback_function(_Atomic(union words3) value);

int consume_callback(plain_callback_function *callback);

int consume_callback(atomic_callback_function *callback) {
  _Atomic(union words3) value =
      (union words3){.words = {19, 11, 12}};
  return callback(value);
}

int main(void) {
  return 0;
}

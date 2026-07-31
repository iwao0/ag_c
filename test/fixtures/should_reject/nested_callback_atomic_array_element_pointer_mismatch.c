typedef int plain_callback_function(int values[1]);
typedef int atomic_callback_function(_Atomic(int) values[1]);

static int consume_callback(atomic_callback_function *callback) {
  _Atomic(int) values[1] = {42};
  return callback(values);
}

int main(void) {
  int (*invalid)(plain_callback_function *) = consume_callback;
  return invalid == 0;
}

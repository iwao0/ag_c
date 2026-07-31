typedef int target_function(int value);
typedef int plain_callback_function(target_function *target);
typedef int atomic_callback_function(_Atomic(target_function *) target);

static int consume_callback(atomic_callback_function *callback) {
  _Atomic(target_function *) target = 0;
  return callback(target);
}

int main(void) {
  int (*invalid)(plain_callback_function *) = consume_callback;
  return invalid == 0;
}

typedef int target_function(int value);
typedef int plain_callback_function(target_function *target);
typedef int atomic_callback_function(_Atomic(target_function *) target);

int consume_callback(plain_callback_function *callback);

int consume_callback(atomic_callback_function *callback) {
  _Atomic(target_function *) target = 0;
  return callback(target);
}

int main(void) {
  return 0;
}

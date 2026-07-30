typedef int plain_result_callback_t(_Atomic(int) value);

int function_parameter_callback_atomic_result(
    plain_result_callback_t *callback, _Atomic(int) value) {
  return callback(value);
}

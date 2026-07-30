typedef _Atomic(int) plain_parameter_callback_t(int value);

int function_parameter_callback_atomic_parameter(
    plain_parameter_callback_t *callback, _Atomic(int) value) {
  _Atomic(int) result = callback(value);
  return result;
}

typedef _Atomic(int) atomic_result_callback_t(
    _Atomic(int) value);

int function_parameter_callback_atomic_result(
    atomic_result_callback_t *callback, _Atomic(int) value);

static _Atomic(int) identity(_Atomic(int) value) {
  return value;
}

int main(void) {
  _Atomic(int) value = 42;
  return function_parameter_callback_atomic_result(
      identity, value);
}

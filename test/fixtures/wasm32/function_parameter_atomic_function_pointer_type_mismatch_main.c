typedef int atomic_parameter_callback_t(int);

int function_parameter_atomic_function_pointer(
    _Atomic(atomic_parameter_callback_t *) callback);

static int identity(int value) {
  return value;
}

int main(void) {
  _Atomic(atomic_parameter_callback_t *) callback = identity;
  return function_parameter_atomic_function_pointer(callback);
}

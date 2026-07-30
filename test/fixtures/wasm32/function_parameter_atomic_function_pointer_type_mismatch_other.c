typedef int atomic_parameter_callback_t(int);

int function_parameter_atomic_function_pointer(
    atomic_parameter_callback_t *callback) {
  return callback(42);
}

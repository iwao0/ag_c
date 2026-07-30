// Paired with function_parameter_atomic_callback_enum_parameter_mismatch_main.c.

enum atomic_callback_parameter_actual_enum {
  ATOMIC_CALLBACK_PARAMETER_ACTUAL_ZERO = 0,
  ATOMIC_CALLBACK_PARAMETER_ACTUAL_VALUE = 42
};

typedef int atomic_callback_parameter_t(
    enum atomic_callback_parameter_actual_enum value);

int apply_atomic_callback_enum_parameter(
    _Atomic(atomic_callback_parameter_t *) callback) {
  return callback(ATOMIC_CALLBACK_PARAMETER_ACTUAL_VALUE);
}

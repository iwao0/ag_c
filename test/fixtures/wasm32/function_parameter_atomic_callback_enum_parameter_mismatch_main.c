enum atomic_callback_parameter_expected_enum {
  ATOMIC_CALLBACK_PARAMETER_EXPECTED_ZERO = 0,
  ATOMIC_CALLBACK_PARAMETER_EXPECTED_VALUE = 42
};

typedef int atomic_callback_parameter_t(
    enum atomic_callback_parameter_expected_enum value);

int apply_atomic_callback_enum_parameter(
    _Atomic(atomic_callback_parameter_t *) callback);

static int read_expected_atomic_callback_parameter(
    enum atomic_callback_parameter_expected_enum value) {
  return value;
}

int main(void) {
  _Atomic(atomic_callback_parameter_t *) callback =
      read_expected_atomic_callback_parameter;
  return apply_atomic_callback_enum_parameter(callback);
}

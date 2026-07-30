// Paired with function_return_atomic_callback_enum_result_mismatch_main.c.

enum atomic_callback_result_actual_enum {
  ATOMIC_CALLBACK_RESULT_ACTUAL_ZERO = 0,
  ATOMIC_CALLBACK_RESULT_ACTUAL_VALUE = 42
};

typedef enum atomic_callback_result_actual_enum
    atomic_callback_result_t(int value);

static enum atomic_callback_result_actual_enum
make_actual_atomic_callback_result(int value) {
  return (enum atomic_callback_result_actual_enum)value;
}

_Atomic(atomic_callback_result_t *)
get_atomic_callback_enum_result(void) {
  return make_actual_atomic_callback_result;
}

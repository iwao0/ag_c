enum atomic_callback_result_expected_enum {
  ATOMIC_CALLBACK_RESULT_EXPECTED_ZERO = 0,
  ATOMIC_CALLBACK_RESULT_EXPECTED_VALUE = 42
};

typedef enum atomic_callback_result_expected_enum
    atomic_callback_result_t(int value);

_Atomic(atomic_callback_result_t *)
get_atomic_callback_enum_result(void);

int main(void) {
  _Atomic(atomic_callback_result_t *) callback =
      get_atomic_callback_enum_result();
  return callback(42);
}

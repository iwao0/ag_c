enum global_atomic_callback_unsigned_enum {
  GLOBAL_ATOMIC_CALLBACK_UNSIGNED_ZERO = 0,
  GLOBAL_ATOMIC_CALLBACK_UNSIGNED_VALUE = 42
};

typedef int global_atomic_callback_t(
    enum global_atomic_callback_unsigned_enum value);

extern _Atomic(global_atomic_callback_t *)
    global_atomic_callback_enum_parameter;

int main(void) {
  return global_atomic_callback_enum_parameter(
      GLOBAL_ATOMIC_CALLBACK_UNSIGNED_VALUE);
}

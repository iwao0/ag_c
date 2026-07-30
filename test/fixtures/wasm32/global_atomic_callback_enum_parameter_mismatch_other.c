// Paired with global_atomic_callback_enum_parameter_mismatch_main.c.

typedef int global_atomic_callback_t(int value);

static int read_global_atomic_callback_parameter(int value) {
  return value;
}

_Atomic(global_atomic_callback_t *)
    global_atomic_callback_enum_parameter =
        read_global_atomic_callback_parameter;

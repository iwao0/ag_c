typedef int global_atomic_callback_t(int);

static int identity(int value) {
  return value;
}

global_atomic_callback_t *global_atomic_function_pointer =
    identity;

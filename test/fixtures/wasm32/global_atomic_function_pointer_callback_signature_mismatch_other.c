typedef int global_atomic_signature_callback_t(unsigned int);

static int identity(unsigned int value) {
  return (int)value;
}

_Atomic(global_atomic_signature_callback_t *)
    global_atomic_function_pointer_callback_signature =
        identity;

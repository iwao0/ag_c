typedef int global_atomic_signature_callback_t(int);

extern _Atomic(global_atomic_signature_callback_t *)
    global_atomic_function_pointer_callback_signature;

int main(void) {
  return global_atomic_function_pointer_callback_signature(42);
}

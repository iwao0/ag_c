typedef int global_atomic_callback_t(int);

extern _Atomic(global_atomic_callback_t *)
    global_atomic_function_pointer;

int main(void) {
  return global_atomic_function_pointer(42);
}

typedef int atomic_result_callback_t(int);

_Atomic(atomic_result_callback_t *)
    function_return_atomic_function_pointer(void);

int main(void) {
  _Atomic(atomic_result_callback_t *) callback =
      function_return_atomic_function_pointer();
  return callback(42);
}

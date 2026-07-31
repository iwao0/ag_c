typedef _Atomic(double _Complex)
    unqualified_result_callback_function(void);

unsigned int consume_qualified_atomic_complex_result(
    unqualified_result_callback_function *callback) {
  (void)callback();
  return 42u;
}

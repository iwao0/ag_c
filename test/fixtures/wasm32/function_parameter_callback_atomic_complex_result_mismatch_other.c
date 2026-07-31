typedef double _Complex plain_result_callback_function(void);

unsigned int consume_atomic_complex_result(
    plain_result_callback_function *callback) {
  return callback() == 0 ? 42u : 0u;
}

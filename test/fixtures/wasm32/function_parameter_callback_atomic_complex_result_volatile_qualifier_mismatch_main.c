typedef volatile _Atomic(double _Complex)
    qualified_result_callback_function(void);

unsigned int consume_qualified_atomic_complex_result(
    qualified_result_callback_function *callback);

static volatile _Atomic(double _Complex)
produce_qualified_complex(void) {
  return 0;
}

int main(void) {
  return (int)consume_qualified_atomic_complex_result(
      produce_qualified_complex);
}

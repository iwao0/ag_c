typedef _Atomic(double _Complex)
    atomic_result_callback_function(void);

unsigned int consume_atomic_complex_result(
    atomic_result_callback_function *callback);

static _Atomic(double _Complex) produce_complex(void) {
  return 0;
}

int main(void) {
  return (int)consume_atomic_complex_result(produce_complex);
}

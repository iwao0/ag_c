typedef _Atomic(float _Complex)
    atomic_result_callback_function(void);

unsigned int consume_atomic_float_complex_result(
    atomic_result_callback_function *callback);

static _Atomic(float _Complex) produce_float_complex(void) {
  return 0;
}

int main(void) {
  return (int)consume_atomic_float_complex_result(
      produce_float_complex);
}

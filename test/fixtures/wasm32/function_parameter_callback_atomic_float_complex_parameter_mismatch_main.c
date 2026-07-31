typedef unsigned int atomic_callback_function(
    _Atomic(float _Complex) value);

unsigned int consume_atomic_float_complex(
    atomic_callback_function *callback);

static unsigned int inspect_float_complex(
    _Atomic(float _Complex) value) {
  return sizeof(value) == 8 ? 42u : 0u;
}

int main(void) {
  return (int)consume_atomic_float_complex(inspect_float_complex);
}

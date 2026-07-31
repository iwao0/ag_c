typedef unsigned int atomic_callback_function(
    _Atomic(double _Complex) value);

unsigned int consume_atomic_complex(
    atomic_callback_function *callback);

static unsigned int inspect_complex(
    _Atomic(double _Complex) value) {
  return sizeof(value) == 16 ? 42u : 0u;
}

int main(void) {
  return (int)consume_atomic_complex(inspect_complex);
}

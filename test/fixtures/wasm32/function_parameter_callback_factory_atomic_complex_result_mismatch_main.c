typedef _Atomic(double _Complex) atomic_target_function(void);
typedef atomic_target_function *atomic_factory_function(void);

unsigned int invoke_atomic_complex_factory(
    atomic_factory_function *factory);

static _Atomic(double _Complex) produce_atomic_complex(void) {
  return 0;
}

static atomic_target_function *make_atomic_complex_target(void) {
  return produce_atomic_complex;
}

int main(void) {
  return (int)invoke_atomic_complex_factory(make_atomic_complex_target);
}

typedef _Atomic(double _Complex) atomic_target_function(void);
typedef atomic_target_function *atomic_factory_function(void);

extern _Atomic(atomic_factory_function *)
    shared_atomic_complex_factory_array[2];

int main(void) {
  atomic_factory_function *factory =
      shared_atomic_complex_factory_array[1];
  atomic_target_function *target = factory();
  _Atomic(double _Complex) value = target();
  return sizeof(value) == 16 ? 42 : 0;
}

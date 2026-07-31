typedef double _Complex plain_target_function(void);
typedef plain_target_function *plain_factory_function(void);

unsigned int invoke_atomic_complex_factory(
    plain_factory_function *factory) {
  (void)factory()();
  return 42u;
}

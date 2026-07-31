typedef double _Complex plain_target_function(void);
typedef plain_target_function *plain_factory_function(void);

static double _Complex produce_plain_complex(void) {
  return 0;
}

static plain_target_function *make_plain_complex_target(void) {
  return produce_plain_complex;
}

_Atomic(plain_factory_function *)
    shared_global_atomic_complex_factory =
        make_plain_complex_target;

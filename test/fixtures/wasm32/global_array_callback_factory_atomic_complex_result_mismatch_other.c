typedef double _Complex plain_target_function(void);
typedef plain_target_function *plain_factory_function(void);

static double _Complex produce_plain_container_complex(void) {
  return 0;
}

static plain_target_function *make_plain_container_complex_target(void) {
  return produce_plain_container_complex;
}

_Atomic(plain_factory_function *)
    shared_atomic_complex_factory_array[2] = {
        make_plain_container_complex_target,
        make_plain_container_complex_target,
};

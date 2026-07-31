union global_factory_words3 {
  unsigned int words[3];
  unsigned char bytes[12];
};

typedef unsigned int atomic_target_function(
    _Atomic(union global_factory_words3) value);
typedef atomic_target_function *atomic_factory_function(void);

extern _Atomic(atomic_factory_function *)
    shared_global_atomic_union_factory;

int main(void) {
  atomic_factory_function *factory =
      shared_global_atomic_union_factory;
  atomic_target_function *target = factory();
  _Atomic(union global_factory_words3) value =
      (union global_factory_words3){.words = {17, 13, 12}};
  return (int)target(value);
}

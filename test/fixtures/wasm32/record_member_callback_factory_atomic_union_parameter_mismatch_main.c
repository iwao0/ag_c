union container_words3 {
  unsigned int words[3];
  unsigned char bytes[12];
};

typedef unsigned int atomic_target_function(
    _Atomic(union container_words3) value);
typedef atomic_target_function *atomic_factory_function(void);

struct factory_holder {
  _Atomic(atomic_factory_function *) member;
};

extern struct factory_holder shared_atomic_factory_holder;

int main(void) {
  atomic_factory_function *factory =
      shared_atomic_factory_holder.member;
  atomic_target_function *target = factory();
  _Atomic(union container_words3) value =
      (union container_words3){.words = {17, 13, 12}};
  return (int)target(value);
}

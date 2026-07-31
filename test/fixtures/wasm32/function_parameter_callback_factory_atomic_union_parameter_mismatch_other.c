union factory_words3 {
  unsigned int words[3];
  unsigned char bytes[12];
};

typedef unsigned int plain_target_function(
    union factory_words3 value);
typedef plain_target_function *plain_factory_function(void);

unsigned int invoke_atomic_union_factory(
    plain_factory_function *factory) {
  plain_target_function *target = factory();
  return target((union factory_words3){.words = {17, 13, 12}});
}

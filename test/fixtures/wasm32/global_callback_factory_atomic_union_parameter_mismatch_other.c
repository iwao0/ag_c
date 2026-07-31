union global_factory_words3 {
  unsigned int words[3];
  unsigned char bytes[12];
};

typedef unsigned int plain_target_function(
    union global_factory_words3 value);
typedef plain_target_function *plain_factory_function(void);

static unsigned int inspect_plain_union(
    union global_factory_words3 value) {
  return value.words[0] + value.words[1] + value.words[2];
}

static plain_target_function *make_plain_union_target(void) {
  return inspect_plain_union;
}

_Atomic(plain_factory_function *)
    shared_global_atomic_union_factory =
        make_plain_union_target;

union container_words3 {
  unsigned int words[3];
  unsigned char bytes[12];
};

typedef unsigned int plain_target_function(
    union container_words3 value);
typedef plain_target_function *plain_factory_function(void);

struct factory_holder {
  _Atomic(plain_factory_function *) member;
};

static unsigned int inspect_plain_container_union(
    union container_words3 value) {
  return value.words[0] + value.words[1] + value.words[2];
}

static plain_target_function *make_plain_container_union_target(void) {
  return inspect_plain_container_union;
}

struct factory_holder shared_atomic_factory_holder = {
    make_plain_container_union_target,
};

union factory_words3 {
  unsigned int words[3];
  unsigned char bytes[12];
};

typedef unsigned int atomic_target_function(
    _Atomic(union factory_words3) value);
typedef atomic_target_function *atomic_factory_function(void);

unsigned int invoke_atomic_union_factory(
    atomic_factory_function *factory);

static unsigned int inspect_atomic_union(
    _Atomic(union factory_words3) value) {
  union factory_words3 snapshot = value;
  return snapshot.words[0] + snapshot.words[1] + snapshot.words[2];
}

static atomic_target_function *make_atomic_union_target(void) {
  return inspect_atomic_union;
}

int main(void) {
  return (int)invoke_atomic_union_factory(make_atomic_union_target);
}

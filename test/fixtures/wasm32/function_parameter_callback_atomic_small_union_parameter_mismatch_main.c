union word1 {
  unsigned int bits;
  float value;
};

typedef unsigned int atomic_callback_function(
    _Atomic(union word1) value);

unsigned int consume_atomic_small_union(
    atomic_callback_function *callback);

static unsigned int inspect_small_union(
    _Atomic(union word1) value) {
  union word1 snapshot = value;
  return snapshot.bits;
}

int main(void) {
  return (int)consume_atomic_small_union(inspect_small_union);
}

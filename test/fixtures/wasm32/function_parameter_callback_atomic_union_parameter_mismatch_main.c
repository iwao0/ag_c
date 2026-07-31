union words3 {
  unsigned int words[3];
  unsigned char bytes[12];
};

typedef unsigned int atomic_callback_function(
    _Atomic(union words3) value);

unsigned int consume_atomic_union(
    atomic_callback_function *callback);

static unsigned int inspect_union(
    _Atomic(union words3) value) {
  union words3 snapshot = value;
  return snapshot.words[0] + snapshot.words[1] + snapshot.words[2];
}

int main(void) {
  return (int)consume_atomic_union(inspect_union);
}

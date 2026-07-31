struct words3 {
  unsigned int first;
  unsigned int second;
  unsigned int third;
};

typedef unsigned int atomic_callback_function(
    _Atomic(struct words3) value);

unsigned int consume_atomic_words(
    atomic_callback_function *callback);

static unsigned int inspect_words(
    _Atomic(struct words3) value) {
  struct words3 snapshot = value;
  return snapshot.first + snapshot.second + snapshot.third;
}

int main(void) {
  return (int)consume_atomic_words(inspect_words);
}

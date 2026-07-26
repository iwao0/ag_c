// Deliberately incompatible with the declaration in the companion main TU:
// this parameter is not atomic.
struct words3 {
  unsigned int a;
  unsigned int b;
  unsigned int c;
};

struct words3 transform_words(struct words3 value) {
  return value;
}

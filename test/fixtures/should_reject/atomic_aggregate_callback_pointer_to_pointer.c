/* Nested callback pointers do not erase an atomic aggregate parameter mismatch. */
struct words3 {
  unsigned int a;
  unsigned int b;
  unsigned int c;
};

typedef struct words3 atomic_callback(_Atomic(struct words3));
typedef struct words3 plain_callback(struct words3);

static struct words3 atomic_identity(_Atomic(struct words3) value) {
  return value;
}

static struct words3 plain_identity(struct words3 value) {
  return value;
}

int main(void) {
  atomic_callback *atomic_value = atomic_identity;
  plain_callback *plain_value = plain_identity;
  atomic_callback **invalid = &plain_value;
  return invalid == &atomic_value;
}

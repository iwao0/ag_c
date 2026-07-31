/* A conditional expression cannot combine atomic and plain aggregate callbacks. */
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
  atomic_callback *left = atomic_identity;
  plain_callback *right = plain_identity;
  return (1 ? left : right) != 0;
}

/* A function with an atomic parameter is incompatible with a plain callback type. */
int apply(int (*callback)(int), int value);

static int atomic_identity(_Atomic int value) {
  return value;
}

int main(void) {
  return apply(atomic_identity, 42);
}

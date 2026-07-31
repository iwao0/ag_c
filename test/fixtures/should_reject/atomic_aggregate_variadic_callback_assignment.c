/* Fixed and variadic atomic aggregate callbacks have incompatible function types. */
struct words3 {
  unsigned int a;
  unsigned int b;
  unsigned int c;
};

typedef struct words3 variadic_callback(_Atomic(struct words3), ...);

static struct words3 fixed_identity(_Atomic(struct words3) value) {
  return value;
}

int main(void) {
  variadic_callback *invalid = fixed_identity;
  return invalid != 0;
}

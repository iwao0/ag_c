// Top-level `typedef void (*F)(...)` used as a parameter lost the void return
// signature. Wasm then emitted call_indirect with `(result i32)`, which traps
// when the actual callback returns void.
#include <assert.h>

typedef void (*Visitor)(int *p, void *user);

int value;

static void set7(int *p, void *user) {
  (void)user;
  *p = 7;
}

static void set9(int *p, int ignored) {
  (void)ignored;
  *p = 9;
}

void iter(Visitor fn, void *user) {
  fn(&value, user);
}

int call_local_typedef(void) {
  typedef void (*LocalVisitor)(int *p, int ignored);
  LocalVisitor fn = set9;
  value = 0;
  fn(&value, 0);
  return value;
}

int main(void) {
  value = 0;
  iter(set7, 0);
  assert(value == 7);
  assert(call_local_typedef() == 9);
  return 0;
}

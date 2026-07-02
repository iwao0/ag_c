/* struct member `double *p` must preserve the floating-point pointee kind.
 * Without that metadata, `s.p[i] = 1.5` was lowered as an integer store in
 * wasm/selfhost object output, truncating 1.5 to 1.0. */
#include <assert.h>

struct Box {
  double *vals;
};

int main(void) {
  double xs[2] = {0.0, 0.0};
  struct Box b;
  b.vals = xs;

  b.vals[0] = 1.5;
  b.vals[1] = 2.5;
  assert(b.vals[0] == 1.5);
  assert(b.vals[1] == 2.5);

  struct Box *pb = &b;
  pb->vals[0] = 3.5;
  assert(xs[0] == 3.5);
  assert(pb->vals[1] == 2.5);
  return 0;
}

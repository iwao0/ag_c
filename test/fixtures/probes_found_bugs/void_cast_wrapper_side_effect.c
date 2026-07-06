// `(void)expr` must be an independent cast result. It should evaluate expr for
// side effects without mutating expr's own FP/scalar type metadata.
#include <assert.h>

static int calls;

static double bump(void) {
  calls++;
  return 1.25;
}

int main(void) {
  double d = 2.5;
  (void)d;
  assert(d == 2.5);

  (void)bump();
  assert(calls == 1);

  return 0;
}

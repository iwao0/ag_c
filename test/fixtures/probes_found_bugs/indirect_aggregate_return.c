/* Function pointer call returning an aggregate through a hidden return area.
 * This is intentionally small: it isolates the Wasm object indirect-call ABI
 * path where the return area must be passed before the source-level args. */
#include <assert.h>

struct Big {
  long a;
  long b;
  long c;
};

static struct Big mk(long x) {
  struct Big v = {x, x + 1, x + 2};
  return v;
}

static long apply(struct Big (*fp)(long), long x) {
  struct Big v = fp(x);
  return v.a + v.b + v.c;
}

int main(void) {
  assert(apply(mk, 13) == 42);
  return 0;
}

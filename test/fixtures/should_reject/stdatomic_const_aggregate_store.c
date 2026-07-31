#include <stdatomic.h>

struct Pair {
  int x;
  int y;
};

// Aggregate atomic objects obey the same const write constraint as scalars.
int main(void) {
  const _Atomic(struct Pair) value = (struct Pair){1, 2};
  atomic_store(&value, ((struct Pair){3, 4}));
  return 0;
}

#include <stdatomic.h>

// atomic_store cannot write through a pointer to a const atomic object.
int main(void) {
  const atomic_int value = 1;
  atomic_store(&value, 2);
  return 0;
}

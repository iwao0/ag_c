#include <stdatomic.h>

// atomic_is_lock_free requires a pointer argument, not an atomic value.
int main(void) {
  atomic_int value = 1;
  return atomic_is_lock_free(value);
}

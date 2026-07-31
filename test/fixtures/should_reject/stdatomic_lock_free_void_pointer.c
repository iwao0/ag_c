#include <stdatomic.h>

// atomic_is_lock_free requires a pointer to a complete atomic object type.
int main(void) {
  void *value = 0;
  return atomic_is_lock_free(value);
}

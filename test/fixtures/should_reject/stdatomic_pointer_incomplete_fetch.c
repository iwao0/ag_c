#include <stdatomic.h>

int main(void) {
  _Atomic(void *) pointer = (void *)0;
  (void)atomic_fetch_add(&pointer, 1);
  return 0;
}

#include <stdatomic.h>

int main(void) {
  int values[2] = {0};
  _Atomic(int *) pointer = values;
  (void)atomic_fetch_or(&pointer, 1);
  return 0;
}

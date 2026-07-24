#include <stdatomic.h>

int main(void) {
  int plain_value = 7;
  _Atomic int atomic_value = 11;
  int choose = 1;
  void *invalid =
      choose ? &plain_value : &atomic_value;
  return invalid == 0;
}

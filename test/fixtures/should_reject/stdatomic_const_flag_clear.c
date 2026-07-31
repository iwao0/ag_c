#include <stdatomic.h>

// Clearing an atomic flag modifies it and cannot target a const flag.
int main(void) {
  const atomic_flag flag = ATOMIC_FLAG_INIT;
  atomic_flag_clear(&flag);
  return 0;
}

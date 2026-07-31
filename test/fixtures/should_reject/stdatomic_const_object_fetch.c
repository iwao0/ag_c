#include <stdatomic.h>

// Fetch-and-add performs a read-modify-write and cannot target a const object.
int main(void) {
  const atomic_int value = 1;
  return atomic_fetch_add(&value, 2);
}

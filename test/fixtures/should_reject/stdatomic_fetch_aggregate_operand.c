#include <stdatomic.h>

struct pair {
  int x;
  int y;
};

// An atomic integer fetch operation cannot take an aggregate operand.
int main(void) {
  atomic_int value = 1;
  return atomic_fetch_add(&value, ((struct pair){2, 3}));
}

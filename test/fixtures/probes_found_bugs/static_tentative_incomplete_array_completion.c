#include <assert.h>

/* C11 6.9.2p5 completes this tentative definition as a one-element array. */
static int values[];

int main(void) {
  assert(values[0] == 0);
  values[0] = 42;
  assert(values[0] == 42);
  return 0;
}

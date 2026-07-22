#include <assert.h>

int answer(void);

/* An empty identifier list in a definition is compatible with a prior
 * prototype that has a single void parameter list. */
int answer() { return 42; }

int main(void) {
  assert(answer() == 42);
  return 0;
}

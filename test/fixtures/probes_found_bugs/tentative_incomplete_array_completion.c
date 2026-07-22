#include <assert.h>

int values[];
/* C11 6.9.2p5 completes this tentative definition as a one-element array. */
int fallback[];
extern int values[];
int values[4];
int values[];

int main(void) {
  assert(sizeof(values) == 4 * sizeof(int));
  assert(values[0] == 0 && values[3] == 0);
  values[1] = 20;
  values[3] = 40;
  fallback[0] = 9;
  assert(values[1] + values[3] == 60);
  assert(fallback[0] == 9);
  return 0;
}

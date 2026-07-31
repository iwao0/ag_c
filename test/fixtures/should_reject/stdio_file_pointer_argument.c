/* A pointer to an unrelated object cannot be passed where FILE * is required. */
#include <stdio.h>

int main(void) {
  int value = 0;
  return fclose(&value);
}

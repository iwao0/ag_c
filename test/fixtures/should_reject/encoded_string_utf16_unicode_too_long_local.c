/* A supplementary Unicode character needs a UTF-16 surrogate pair. */
#include <uchar.h>

int main(void) {
  char16_t values[1] = u"\U0001F600";
  return values[0];
}

/* A UTF-16 surrogate pair plus an explicit NUL needs three elements. */
#include <uchar.h>

int main(void) {
  char16_t values[2] = u"\U0001F600\0";
  return values[0];
}

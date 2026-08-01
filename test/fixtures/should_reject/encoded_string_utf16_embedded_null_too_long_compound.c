/* A UTF-16 compound literal cannot discard an explicit NUL code unit. */
#include <uchar.h>

int main(void) {
  char16_t *values = (char16_t[2]){u"\U0001F600\0"};
  return values[0];
}
